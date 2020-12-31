/*
* Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd.
*
* Author:     kongyunzhen <kongyunzhen@uniontech.com>
*
* Maintainer: kongyunzhen <kongyunzhen@uniontech.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "calendarscheduler.h"
#include "src/commondatastruct.h"
#include "src/utils.h"
#include "lunarmanager.h"
#include "pinyin/pinyinsearch.h"
#include "jobremindmanager.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QDebug>

#define RECURENCELIMIT 3650 //递归次数限制
#define UPDATEREMINDJOBTIMEINTERVAL 1000 * 60 * 10 //提醒任务更新时间间隔毫秒数（10分钟）

QMap<QString, quint32> CalendarScheduler::m_festivalIdMap;
quint32 CalendarScheduler::nextFestivalJobId = INT32_MAX;
QMutex CalendarScheduler::m_getJobIdMutex;
CalendarScheduler::CalendarScheduler(QObject *parent)
    : QObject(parent)
    , m_database(new SchedulerDatabase(this))
{
    IsFestivalJobEnabled();
    //将提醒提醒任务管理放入子线程
    m_jobremindmanager = new JobRemindManager;
    threadremind = new QThread(this);
    m_jobremindmanager->moveToThread(threadremind);
    threadremind->start();
    m_timeUpdateRemindJobs = new QTimer(this);
    //轮询间隔十分钟
    m_timeUpdateRemindJobs->setInterval(UPDATEREMINDJOBTIMEINTERVAL);
    initConnections();
    UpdateRemindTimeout();
    m_timeUpdateRemindJobs->start();
}

CalendarScheduler::~CalendarScheduler()
{
    if (!threadremind->isFinished()) {
        m_jobremindmanager->disconnect();
        threadremind->quit();
        threadremind->wait();
    }
    delete m_jobremindmanager;
}

void CalendarScheduler::initConnections()
{
    connect(m_timeUpdateRemindJobs, &QTimer::timeout, this, &CalendarScheduler::UpdateRemindTimeout);
    connect(this, &CalendarScheduler::NotifyUpdateRemindJobs, m_jobremindmanager, &JobRemindManager::UpdateRemindJobs);
    connect(m_jobremindmanager, &JobRemindManager::ModifyJobRemind, this, &CalendarScheduler::OnModifyJobRemind);
    connect(this, &CalendarScheduler::NotifyJobChange, m_jobremindmanager, &JobRemindManager::NotifyJobsChanged);
}

QString CalendarScheduler::GetType(qint64 id)
{
    QString strres;
    foreach (auto var, globalPredefinedTypes) {
        if (var.ID == id) {
            //tojson
            QJsonObject obj;
            obj.insert("ID", static_cast<int>(var.ID));
            obj.insert("Name", var.Name);
            obj.insert("Color", var.Color);
            QJsonDocument doc;
            doc.setObject(obj);
            strres = QString::fromUtf8(doc.toJson());
            break;
        }
    }
    return strres;
}

QString CalendarScheduler::GetTypes()
{
    QString strres;
    QJsonArray jsonArray;
    QJsonDocument doc;
    foreach (auto var, globalPredefinedTypes) {
        QJsonObject obj;
        obj.insert("ID", static_cast<int>(var.ID));
        obj.insert("Name", var.Name);
        obj.insert("Color", var.Color);
        jsonArray.append(obj);
    }
    doc.setArray(jsonArray);
    strres = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    return strres;
}

void CalendarScheduler::DeleteJob(qint64 id)
{
    m_database->DeleteJob(id);
    QList<qlonglong> ids;
    ids.append(id);
    AfterJobChanged(ids);
}

void CalendarScheduler::DeleteType(qint64 id)
{
    m_database->DeleteType(id);
}


QString CalendarScheduler::GetJob(qint64 id)
{
    return m_database->GetJob(id);
}

// 给定日程Json信息，解析为job类型传入数据库
qint64 CalendarScheduler::CreateJob(const QString &jobInfo)
{
    // 现将给的Json信息转为Job类型
    QJsonParseError json_error;
    QJsonDocument jsonDoc(QJsonDocument::fromJson(jobInfo.toLocal8Bit(), &json_error));

    if (json_error.error != QJsonParseError::NoError) {
        return false;
    }

    QJsonObject rootObj = jsonDoc.object();
    Job job;

    if (rootObj.contains("ID")) {
        job.ID = rootObj.value("ID").toInt();
    }
    if (rootObj.contains("Type")) {
        job.Type = rootObj.value("Type").toInt();
    }
    if (rootObj.contains("Title")) {
        job.Title = rootObj.value("Title").toString();
    }
    if (rootObj.contains("Description")) {
        job.Description = rootObj.value("Description").toString();
    }
    if (rootObj.contains("AllDay")) {
        job.AllDay = rootObj.value("AllDay").toBool();
    }
    if (rootObj.contains("Start")) {
        // 此处时间转换为与client同样式
        job.Start = QDateTime::fromString(rootObj.value("Start").toString(), Qt::ISODate);
    }
    if (rootObj.contains("End")) {
        job.End = QDateTime::fromString(rootObj.value("End").toString(), Qt::ISODate);
    }
    if (rootObj.contains("RRule")) {
        job.RRule = rootObj.value("RRule").toString();
    }
    if (rootObj.contains("Remind")) {
        job.Remind = rootObj.value("Remind").toString();
    }
    if (rootObj.contains("Ignore")) {
        QJsonArray subArray = rootObj.value("Ignore").toArray();
        QJsonDocument doc;
        doc.setArray(subArray);
        job.Ignore = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    //添加title拼音
    job.Title_pinyin = pinyinsearch::getPinPinSearch()->CreatePinyin(rootObj.value("Title").toString());

    qint64 id = m_database->CreateJob(job);
    QList<qlonglong> ids;
    ids.append(id);
    AfterJobChanged(ids);
    return id;
}

// 可将要改动的日程信息直接传入数据库操作层中
void CalendarScheduler::UpdateJob(const QString &jobInfo)
{
    qint64 id = m_database->UpdateJob(jobInfo);
    if (-1 != id) {
        QList<qlonglong> ids;
        ids.append(id);
        AfterJobChanged(ids);
    }
}

// 可将要改动的日程类型信息直接传入数据库操作层中
void CalendarScheduler::UpdateType(const QString &typeInfo)
{
    m_database->UpdateType(typeInfo);
}

/**
 * @brief  GetJobs 获取指定范围内的日程
 * @param start 起始时间
 * @param end 结束时间
 * @return 返回指定范围内的日程JSON格式
 */
QString CalendarScheduler::GetJobs(const QDateTime &start, const QDateTime &end)
{
    QString strJson;
    QList<Job> joblist = m_database->GetAllOriginJobs();
    QList<stJobArr> jobArrList = GetJobsBetween(start, end, joblist, m_festivalJobEnabled); //获取时间范围内所有的Job
    strJson = JobArrListToJsonStr(jobArrList);
    return strJson;
}

/**
 * @brief  QueryJobs 查询指定时间范围日程
 * @param params 查询条件json字符串
 * @return 返回指定范围内的日程JSON格式
 */
QString CalendarScheduler::QueryJobs(const QString &params)
{
    QJsonDocument doc;
    QJsonParseError err;
    doc = QJsonDocument::fromJson(params.toUtf8(), &err);
    QJsonObject obj = doc.object();
    QString strKey = obj.value("Key").toString();
    QDateTime starttime = Utils::fromconvertData(obj.value("Start").toString());
    QDateTime endtime = Utils::fromconvertData(obj.value("End").toString());
    QList<Job> joblist = m_database->GetAllOriginJobs(strKey);
    QList<stJobArr> jobArrList = GetJobsBetween(starttime, endtime, joblist, m_festivalJobEnabled, strKey);
    QString strJson = JobArrListToJsonStr(jobArrList);
    return strJson;
}

QString CalendarScheduler::QueryJobsWithLimit(const QString &params, qint32 maxNum)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8(), &err);
    QJsonObject obj = doc.object();
    QDateTime start = Utils::fromconvertData(obj.value("Start").toString());
    QDateTime end = Utils::fromconvertData(obj.value("End").toString());
    QString strKey = obj.value("key").toString();

    QList<Job> joblist = m_database->GetAllOriginJobs(strKey, QString("start asc"));
    //获取时间范围内符合条件的Job，不扩展
    QList<stJobArr> jobArrList = GetJobsBetween(start, end, joblist, false, strKey, false);
    jobArrList = FilterDateJobsWrap(jobArrList, start, end);
    //根据manNum获取指定数量的Job多余的直接丢弃
    int jobCount = 0;
    QList<stJobArr> resArr;
    foreach (stJobArr jobarr, jobArrList) {
        if (jobarr.jobs.size() > 0) {
            int jobsLength = jobarr.jobs.size();
            jobCount += jobsLength;
            if (jobCount >= maxNum) {
                int edge = jobsLength - (jobCount - maxNum);
                QList<Job> jobs;
                for (int i = 0; i < edge; ++i) {
                    jobs.append(jobarr.jobs.at(i));
                }
                jobarr.jobs = jobs;
                resArr.append(jobarr);
                break;
            }
            resArr.append(jobarr);
        }
    }
    QString strJson = JobArrListToJsonStr(resArr);

    return strJson;
}

QString CalendarScheduler::QueryJobsWithRule(const QString &params, const QString &rules)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(params.toUtf8(), &err);
    QJsonObject obj = doc.object();
    QDateTime start = Utils::fromconvertData(obj.value("Start").toString());
    QDateTime end = Utils::fromconvertData(obj.value("End").toString());
    QString strKey = obj.value("key").toString();
    QList<Job> joblist = m_database->GetAllOriginJobsWithRule(strKey, rules);
    //获取时间范围内符合条件的Job，不扩展
    QList<stJobArr> jobArrList = GetJobsBetween(start, end, joblist, false, strKey, false);
    jobArrList = FilterDateJobsWrap(jobArrList, start, end);
    QString strJson = JobArrListToJsonStr(jobArrList);
    return strJson;
}

//检测当前环境是否为中文环境来决定是否开启获取节日日程开关
void CalendarScheduler::IsFestivalJobEnabled()
{
    QString str_lcall(qgetenv("LC_ALL").data());
    QString str_language = qgetenv("LANGUAGE");
    QString str_lang = qgetenv("LANG");
    qDebug() << str_lcall << str_language << str_lang;
    if (("C" != str_lcall) && (str_language.startsWith("zh") || str_lang.startsWith("zh"))) {
        m_festivalJobEnabled = true;
    }
}

/**
 * @brief CalendarScheduler::GetJobsBetween 对查询到的日程进行解析处理
 * @param start 查询的开始时间
 * @param end 查询的结束时间
 * @param joblist 查询到的日程list
 * @param needFestival 是否需要返回节日日程
 * @param querykey 查询的关键字
 * @param bextend 是否需要扩展跨天日程
 * @return  日程list
 */
QList<stJobArr> CalendarScheduler::GetJobsBetween(const QDateTime &start, const QDateTime &end, const QList<Job> &joblist, bool needFestival, const QString &querykey, bool bextend)
{
    QList<stJobArr> jobArrList;
    int days = static_cast<int>(start.daysTo(end));
    for (int i = 0; i <= days; ++i) {
        stJobArr arr;
        QDateTime tem = start.addDays(i);
        arr.date = tem.date();
        jobArrList.append(arr);
    }
    //判断是否需要获取中国节日日程
    if (needFestival) {
        FillFestivalJobs(start, end, querykey, jobArrList);
    }

    foreach (Job jb, joblist) {
        qint64 interval = jb.Start.secsTo(jb.End); //当前job开始结束时间差，单位秒
        QList<stJobTime> jobtimelist = GetJobTimesBetween(start, end, jb);
        if (jobtimelist.isEmpty()) {
            continue;
        }
        foreach (stJobTime jobtime, jobtimelist) {
            Job jobtem = jb;
            jobtem.Start = jobtime.start;
            jobtem.End = jobtime.start.addSecs(interval);
            jobtem.RecurID = jobtime.recurID;
            int idx = static_cast<int>(start.daysTo(jobtime.start));
            if (idx >= 0 && idx <= days) {
                jobArrList[idx].jobs.append(jobtem);
            }

            //  extend 指把跨越多天的 job 给扩展开，比如开始日期为 2019-09-01 结束日期为 2019-09-02 的
            // job, 将会扩展出一个job 放入 2019-09-02 那天的 extendJobs 字段中。
            if (!bextend) {
                continue;
            }
            //需要扩展的天数
            int extenddays = static_cast<int>(jobtem.Start.daysTo(jobtem.End));
            for (int i = 0; i < extenddays; ++i) {
                int tIdx = idx + i + 1;
                if (tIdx == jobArrList.size()) {
                    break;
                }
                if (tIdx < 0) {
                    continue;
                }
                jobArrList[tIdx].extends.append(jobtem);
            }
        }
    }
    return jobArrList;
}

QList<stJobTime> CalendarScheduler::GetJobTimesBetween(const QDateTime &start, const QDateTime &end, const Job &job)
{
    QList<stJobTime> jobtimelist;
    if (start <= end) {
        QList<QDateTime> igonrelist = GetIgnoreList(job);
        //如果没有规则，则直接判断其开始结束时间就可以了
        if (job.RRule.isEmpty()) {
            //开始时间小于job的结束时间并且结束时间要大于job的开始时间，保证有交集,保证
            if (start <= job.End && end >= job.Start) {
                stJobTime jobtime;
                jobtime.start = job.Start;
                jobtime.end = job.End;
                jobtime.recurID = 0;
                jobtimelist.append(jobtime);
            }
            return jobtimelist;
        }

        int count = 0; //当前为日程的第几次重复
        stRRuleOptions options = ParseRRule(job.RRule);
        QDateTime jobstart = job.Start; //当前原始job的起始时间
        QDateTime jobend = job.End; //当前原始job的结束时间
        int dateinterval = static_cast<int>(jobstart.secsTo(jobend)); //job的开始结束间隔日期

        int dayofweek = jobstart.date().dayOfWeek(); //判断是周几
        if (dayofweek > Qt::Friday && options.rpeat == RepeatType::RepeatWorkDay) //周末并且options为工作日重复
            jobstart.setDate(jobstart.date().addDays(7 - dayofweek + 1)); //在周末设置工作日重复日程，需要重新设置日程开始时间

        QDateTime next = jobstart; //next为下一新建日程起始日期
        //只有当下一个新建日程的起始日期小于查询日期的结束日期才会有交集，否则没有意义
        //注意一定要判断是否有交集，因为Job的创建日期可能远远早于查询日期，查询到的是多次重复后与查询时间有交集的
        while (true) {
            QDateTime copystart = next;
            //这里应该比较date，而不是datetime，如果是非全天的日程，这个设计具体时间的问题，会导致返回的job个数出现问题
            if (copystart.date() > end.date()) {
                //起始日期超出查询结束日期直接退出
                break;
            }

            QDateTime copyend = next.addSecs(dateinterval);
            //如果查询时间范围和Job时间范围有交集则保存
            //另外需要保证该新建任务没有被删除即未被ignore，新建任务重复的规则删除是删除该次重复包含的的所有天，
            if (OverLap(start, end, copystart, copyend) && !ContainsInIgnoreList(igonrelist, copystart)) {
                stJobTime jt;
                jt.start = copystart;
                jt.end = copyend;
                jt.recurID = count;
                jobtimelist.append(jt);
            }
            count++;
            //当结束重复为按多少次结束判断时，检查重复次数是否达到，达到则退出
            //当重复次数达到最大限制直接返回
            //options.tcount表示重复的次数，而count表示总次数，所以这里不能有“=”
            if ((options.type == RepeatOverCount && options.tcount < count)
                    || count > RECURENCELIMIT) {
                break;
            }
            //根据rule获取下一个Job的起始日期
            next = GetNextJobStartTimeByRule(options, copystart);
            //判断next是否有效,时间大于RRule的until
            //判断next是否大于查询的截止时间,这里应该比较date，而不是datetime，如果是非全天的日程，这个设计具体时间的问题，会导致返回的job个数出现问题
            if ((options.type == RepeatOverUntil && next >= options.overdate)
                || next.date() > end.date()) {
                break;
            }
            copystart = next;
        }
    } else {
        qDebug() << __FUNCTION__ << "start time later than end time param error! do nothoing";
    }

    return jobtimelist;
}

/**
 * @brief  FillFestivalJobs 获取指定范围内的节日日程信息
 * @param start 起始时间
 * @param end 结束时间
 */
void CalendarScheduler::FillFestivalJobs(const QDateTime &start, const QDateTime &end, const QString &querykey, QList<stJobArr> &listjob)
{
    QList<stDayFestival> festivaldays = GetFestivalsInRange(start, end);

    if (!querykey.isEmpty()) {
        festivaldays = FilterDayFestival(festivaldays, querykey);
    }

    foreach (stDayFestival day, festivaldays) {
        int index = static_cast<int>(start.daysTo(day.date));
        if (index >= 0 && index < listjob.size()) {
            foreach (QString festival, day.Festivals) {
                if (!festival.isEmpty()) {
                    Job jb;
                    jb.Title = festival;
                    jb.Type = JobTypeFestival;
                    jb.AllDay = true;
                    jb.Start = QDateTime(QDate(day.date.date()), QTime(0, 0));
                    jb.End = QDateTime(QDate(day.date.date()), QTime(23, 59));
                    jb.RRule = "FREQ=YEARLY";
                    jb.ID = GetFestivalId(festival);
                    listjob[index].jobs.append(jb);
                }
            }
        }
    }
}

/**
 * @brief  GetIgnoreList 获取指定Job的忽略列表
 * @param job 查询的Job
 * @return 当前Job的忽略列表
 */
QList<QDateTime> CalendarScheduler::GetIgnoreList(const Job &job)
{
    QList<QDateTime> list;
    if (!job.Ignore.isEmpty()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(job.Ignore.toLocal8Bit(), &err);
        if (QJsonParseError::NoError == err.error) {
            QJsonArray arr = doc.array();
            for (int i = 0; i < arr.count(); ++i) {
                QString str = arr.at(i).toString();
                QDateTime datetime = QDateTime::fromString(str, Qt::ISODate);
                list.append(datetime);
            }
        }
    }
    return list;
}

/**
 * @brief  ContainsInIgnoreList 检查日期是否在忽略列表里
 * @param ignorelist 日期忽略列表
 * @param start 需要检测时间
 * @return bool 如果在忽略列表中则返回true
 */
bool CalendarScheduler::ContainsInIgnoreList(const QList<QDateTime> ignorelist, const QDateTime &time)
{
    return ignorelist.contains(time);
}

/**
 * @brief  OverLap 判断两个时间范围是否有交集
 * @param start 第一个时间范围的开始时间
 * @param end 第一个时间范围的结束时间
 * @param jobstart 第二个时间范围的开始时间
 * @param jobend 第二个时间范围的结束时间
 * @return bool 有交集返回true否则返回false
 */
bool CalendarScheduler::OverLap(const QDateTime &start, const QDateTime &end, const QDateTime &jobstart, const QDateTime &jobend)
{
    bool boverlap = false;
    if ((start <= jobstart && end >= jobstart)
        || (start >= jobstart && start <= jobend)) {
        boverlap = true;
    }

    return boverlap;
}

/**
 * @brief  GetNextJobStartTimeByRule 根据规则获取下一个job开始时间
 * @param options 包含重复规则相关字段结构体
 * @param datetime 上一个Job的时间
 * @return QDateTime 下一个重复Job的起始时间
 */
QDateTime CalendarScheduler::GetNextJobStartTimeByRule(const stRRuleOptions &options, const QDateTime &datetime)
{
    QDateTime next;
    quint8 dayofweek;
    //日程所在年
    int year = datetime.date().year();
    //日程所在月
    int month = datetime.date().month();
    //日程所在日
    int day = datetime.date().day();
    //日程时间
    QTime nextTime = datetime.time();
    //判断next日期是否合法，需要先给next赋值，所以使用do-while
    do {
        switch (options.rpeat) {
        case RepeatDaily:
            next = datetime.addDays(1);
            break;
        case RepeatWorkDay:
            dayofweek = static_cast<quint8>(datetime.date().dayOfWeek());
            //计算当前为周几如果是周五或者是周末需要跳过，否则直接下一天
            //需要跳过因为工作日不包括周末
            if (dayofweek >= Qt::Friday) {
                next = datetime.addDays(7 - dayofweek + 1);
            } else {
                next = datetime.addDays(1);
            }
            break;
        case RepeatWeekly:
            next = datetime.addDays(7);
            break;
        case RepeatMonthly:
            //月份超过12月，则进入下一年
            if (++month == 13) {
                year += 1;
                month = 1;
            }
            next = QDateTime(QDate(year, month, day), nextTime);
            break;
        case RepeatYearly:
            next = QDateTime(QDate(++year, month, day), nextTime);
            break;
        default:
            break;
        }
    } while (!next.isValid());

    return next;
}

/**
 * @brief  ParseRRule 解析重复规则
 * @param rule 规则字符串
 * @return stRRuleOptions 包含重复规则相关字段的结构体
 */
stRRuleOptions CalendarScheduler::ParseRRule(const QString &rule)
{
    //无规则的不走这里判断所以此处默认rule不为空
    //局部变量初始化
    stRRuleOptions options {};
    QStringList rruleslist = rule.split(";", QString::SkipEmptyParts);
    //rpeat重复规则 0 无  1 每天 2 每个工作日 3 每周 4每月 5每年
    //type结束重复类型 0 永不 1  多少次结束  2 结束日期
    if (rruleslist.contains("FREQ=DAILY") && rruleslist.contains("BYDAY=MO,TU,WE,TH,FR")) {
        options.rpeat = RepeatWorkDay;
    } else if (rruleslist.contains("FREQ=DAILY")) {
        options.rpeat = RepeatDaily;
    } else if (rruleslist.contains("FREQ=WEEKLY")) {
        options.rpeat = RepeatWeekly;
    } else if (rruleslist.contains("FREQ=MONTHLY")) {
        options.rpeat = RepeatMonthly;
    } else if (rruleslist.contains("FREQ=YEARLY")) {
        options.rpeat = RepeatYearly;
    }

    for (int i = 0; i < rruleslist.count(); i++) {
        if (rruleslist.at(i).contains("COUNT=")) {
            QStringList liststr = rruleslist.at(i).split("=", QString::SkipEmptyParts);
            options.type = RepeatOverCount;
            options.tcount = liststr.at(1).toInt() - 1;
        }

        if (rruleslist.at(i).contains("UNTIL=")) {
            QStringList liststr = rruleslist.at(i).split("=", QString::SkipEmptyParts);
            options.type = RepeatOverUntil;
            options.overdate = QDateTime::fromString(liststr.at(1).left(liststr.at(1).count() - 1), "yyyyMMddThhmmss");
            options.overdate = options.overdate.addDays(1);
        }
    }
    return options;
}

/**
 * @brief  JobArrListToJsonStr 将jobArrList转化为json字符串
 * @param jobArrList jobArrList
 * @return json字符串
 */
QString CalendarScheduler::JobArrListToJsonStr(const QList<stJobArr> &jobArrList)
{
    QString strJson;
    QJsonDocument doc;
    QJsonArray jsonarr;
    foreach (stJobArr jobarr, jobArrList) {
        QJsonObject obj;
        QJsonArray jobsJsonArr;
        QJsonObject objjob;
        obj.insert("Date", jobarr.date.toString("yyyy-MM-dd"));
        foreach (Job job, jobarr.jobs) {
            objjob = Utils::JobToObject(job);
            jobsJsonArr.append(objjob);
        }
        foreach (Job job, jobarr.extends) {
            objjob = Utils::JobToObject(job);
            jobsJsonArr.append(objjob);
        }
        obj.insert("Jobs", jobsJsonArr);
        jsonarr.append(obj);
    }
    doc.setArray(jsonarr);
    strJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    return strJson;
}

QList<Job> CalendarScheduler::GetRemindJobs(const QDateTime &start, const QDateTime &end)
{
    m_timeUpdateRemindJobs->stop();
    QList<Job> jobs;
    jobs = m_database->GetJobsContainRemind();
    QDateTime endDate = start.addDays(8);
    QList<stJobArr> jobArrList = GetJobsBetween(start, endDate, jobs, false, "", false); //此处获取的数据不需要展开，因此extend为空
    if (jobArrList.size() > 0) {
        jobs.clear();
        foreach (stJobArr jobarr, jobArrList) {
            foreach (Job job, jobarr.jobs) {
                QDateTime jbtm = GetJobRemindTime(job);
                //如果计算出的时间无效则忽略该Job继续
                if (!jbtm.isValid()) {
                    continue;
                }
                if (jbtm >= start && jbtm <= end) {
                    job.RemidTime = jbtm;
                    jobs.append(job);
                }
            }
        }
    }
    m_timeUpdateRemindJobs->start();
    return jobs;
}

/**
 * @brief  GetJobRemindTime 计算job的提醒时间
 * @param job 日程
 * @return 日程提醒时间，调用方需判断日程时间是否有效
 */
QDateTime CalendarScheduler::GetJobRemindTime(const Job &job)
{
    QDateTime tm = job.Start;
    if (job.AllDay) {
        tm.setTime(QTime(0, 0, 0));
    }
    return ParseRemind(tm, job.Remind);
}

/**
 * @brief  ParseRemind 根据规则和给定的任务时间计算提醒时间
 * @param tm 日程开始时间
 * @param strremind 日程提醒规则
 * @return 日程提醒时间，调用方需判断日程时间是否有效
 */
QDateTime CalendarScheduler::ParseRemind(const QDateTime &tm, const QString &strremind)
{
    // 提醒的提前时间最大为 7 天
    //创建一个无效的时间，如果计算出有效时间则根据计算结果更新否则返回无效结果用于调用方判断
    QDateTime remindtm(QDate(2020, 13, 31), QTime(24, 24, 100));
    if (!strremind.isEmpty()) {
        QRegExp reg("\\d;\\d\\d:\\d\\d");
        // QRegExp reg("\\d;\\d+?:\\d+?");
        if (reg.exactMatch(strremind)) {
            //提前多少天
            qint64 nDays;
            //提醒时间
            int hour, min;
            int ret = sscanf(strremind.toStdString().c_str(), "%lld;%d:%d", &nDays, &hour, &min);
            //判断解析出来的规则是否合法
            //对ndays，hour，min的判断应该包含边界值
            if (-1 != ret && nDays >= 0 && nDays <= 7 && hour >= 0 && hour <= 23 && min >= 0 && min <= 59) {
                remindtm = tm.addDays(-nDays); //多少天前
                remindtm.setTime(QTime(hour, min, 0));
            }
        } else {
            bool bsuccess = false;
            //一天以内的时间单位是分钟
            qint32 nMinutes = strremind.toInt(&bsuccess);
            //nMinutes为非全天日程提醒的分钟数，应该包含边界值
            if (bsuccess && (nMinutes >= 0 && nMinutes <= 60 * 24 * 7)) {
                remindtm = tm.addSecs(-nMinutes * 60); //将分转换成秒进行回退
            }
        }
    }

    return remindtm;
}

void CalendarScheduler::AfterJobChanged(const QList<qlonglong> &Ids)
{
    //发送更新jobs信号
    emit JobsUpdated(Ids);
    emit NotifyJobChange(Ids);
    QDateTime tmstart = QDateTime::currentDateTime();
    QDateTime tmend = tmstart.addMSecs(UPDATEREMINDJOBTIMEINTERVAL);
    emit NotifyUpdateRemindJobs(GetRemindJobs(tmstart, tmend));
}

QList<stJobArr> CalendarScheduler::FilterDateJobsWrap(const QList<stJobArr> &arrList, const QDateTime &start, const QDateTime &end)
{
    QList<stJobArr> wraplist;
    foreach (stJobArr jobarr, arrList) {
        QList<Job> jobs, extendjobs;
        foreach (Job jb1, jobarr.jobs) {
            if (OverLap(start, end, jb1.Start, jb1.End)) {
                jobs.append(jb1);
            }
        }
        foreach (Job jb2, jobarr.extends) {
            if (OverLap(start, end, jb2.Start, jb2.End)) {
                extendjobs.append(jb2);
            }
        }
        if (jobs.size() + extendjobs.size() > 0) {
            jobarr.jobs = jobs;
            jobarr.extends = extendjobs;
            wraplist.append(jobarr);
        }
    }

    return wraplist;
}

void CalendarScheduler::UpdateRemindTimeout()
{
    QDateTime tmstart = QDateTime::currentDateTime();
    QDateTime tmend = tmstart.addMSecs(UPDATEREMINDJOBTIMEINTERVAL);
    emit NotifyUpdateRemindJobs(GetRemindJobs(tmstart, tmend));
}

void CalendarScheduler::OnModifyJobRemind(const Job &job, const QString &remind)
{
    Job newjob = job;
    QList<qlonglong> ids;
    //规则不为空则创建一个新的日程
    if (!newjob.RRule.isEmpty()) {
        //修改重复日程的提醒规则相当于“仅修改此日程”的规则，忽略当天的日程，新建一个没有重复规则的日程
        //修改新日程的提醒规则
        newjob.Remind = remind;
        //清空日程的重复规则
        newjob.RRule.clear();
        //设置初始值
        newjob.RecurID = 0;
        QList<QDateTime> ignorelist = GetIgnoreList(job);
        bool bsuccess = true;
        //判断是否在忽略列表，如果不在忽略列表则加入忽略列表，后面重新创建新日程替代该旧日程
        if (!ignorelist.contains(job.Start)) {
            ignorelist.append(job.Start);
            QJsonDocument doc;
            QJsonArray arr;
            foreach (QDateTime tm, ignorelist) {
                arr.append(tm.toString(Qt::ISODate));
            }
            doc.setArray(arr);
            QString strignorejson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
            bsuccess = m_database->UpdateJobIgnore(strignorejson, job.ID);
        }
        newjob.ID = m_database->CreateJob(newjob);
        if (bsuccess && -1 != newjob.ID) {
            ids << job.ID << newjob.ID;
            AfterJobChanged(ids);
            emit JobsUpdated(ids);
        }
    } else {
        ids.append(newjob.ID);
        AfterJobChanged(ids);
        emit JobsUpdated(ids);
    }
}

/**
 * @brief  GetFestivalId 根据节日名称获取节日id
 * @param name 节日名称
 * @return 节日id
 */
quint32 CalendarScheduler::GetFestivalId(const QString &name)
{
    //自动加锁，函数结束销毁时自动解锁
    QMutexLocker lock(&m_getJobIdMutex);
    quint32 id = 0;
    auto it = m_festivalIdMap.find(name);
    if (it != m_festivalIdMap.end()) {
        id = it.value();
    } else {
        id = nextFestivalJobId;
        m_festivalIdMap.insert(name, id);
        nextFestivalJobId--;
    }
    return id;
}