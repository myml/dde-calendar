/*
 * Copyright (C) 2015 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     kirigaya <kirigaya@mkacg.com>
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

#include "monthdayview.h"
#include "scheduledatamanage.h"
#include "constants.h"

#include <DPalette>
#include <DHiDPIHelper>

#include <QHBoxLayout>
#include <QPainter>
#include <QBrush>
#include <QEvent>
#include <QMouseEvent>
#include <QDebug>

DGUI_USE_NAMESPACE
CMonthDayView::CMonthDayView(QWidget *parent)
    : DFrame(parent)
    , m_touchGesture(this)
{
    QHBoxLayout *hboxLayout = new QHBoxLayout;
    hboxLayout->setMargin(0);
    hboxLayout->setSpacing(0);
    hboxLayout->setContentsMargins(10, 0, 10, 0);
    m_monthWidget = new CMonthWidget(this);
    hboxLayout->addWidget(m_monthWidget);
    setLayout(hboxLayout);
    setFrameRounded(true);
    setLineWidth(0);
    setWindowFlags(Qt::FramelessWindowHint);

    connect(m_monthWidget, &CMonthWidget::signalsSelectDate, this, &CMonthDayView::signalsSelectDate);
}

CMonthDayView::~CMonthDayView()
{
}

/**
 * @brief CMonthDayView::setSelectDate  设置选择时间
 * @param date
 */
void CMonthDayView::setSelectDate(const QDate &date)
{
    m_selectDate = date;
    for (int i  = 0; i < DDEMonthCalendar::MonthNumofYear; ++i) {
        m_days[i] = m_selectDate.addMonths(i - 5);
    }
    m_monthWidget->setDate(m_days);
    update();
}

/**
 * @brief CMonthDayView::setTheMe   设置主题颜色
 * @param type
 */
void CMonthDayView::setTheMe(int type)
{
    QColor frameclor;
    if (type == 0 || type == 1) {
        frameclor = "#FFFFFF";
    } else if (type == 2) {
        frameclor = "#FFFFFF";
        frameclor.setAlphaF(0.05);
    }
    DPalette anipa = palette();
    anipa.setColor(DPalette::Background, frameclor);
    setPalette(anipa);
    setBackgroundRole(DPalette::Background);
    CMonthRect::setTheMe(type);
}

void CMonthDayView::setsearchfalg(bool flag)
{
    m_searchfalg = flag;
}

void CMonthDayView::wheelEvent(QWheelEvent *e)
{
    //如果滚动为左右则触发信号
    if (e->orientation() == Qt::Orientation::Horizontal) {
        emit signalAngleDelta(e->angleDelta().x());
    } else {
        emit signalAngleDelta(e->angleDelta().y());
    }
}

bool CMonthDayView::event(QEvent *e)
{
    if (m_touchGesture.event(e)) {
        //获取触摸状态
        switch (m_touchGesture.getTouchState()) {
        case touchGestureOperation::T_SLIDE: {
            //在滑动状态如果可以更新数据则切换月份
            if (m_touchGesture.isUpdate()) {
                m_touchGesture.setUpdate(false);
                switch (m_touchGesture.getMovingDir()) {
                case touchGestureOperation::T_LEFT:
                    emit signalAngleDelta(-1);
                    break;
                case touchGestureOperation::T_RIGHT:
                    emit signalAngleDelta(1);
                    break;
                default:
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
        return true;
    } else {
        return DFrame::event(e);
    }
}
/**
 * @brief CMonthWidget 构造函数
 * @param parent 父类
 */
CMonthWidget::CMonthWidget(QWidget *parent)
    : QWidget(parent)
{
    for (int i = 0; i < DDEMonthCalendar::MonthNumofYear; ++i) {
        CMonthRect *monthrect = new CMonthRect();
        m_MonthItem.append(monthrect);
    }
}

CMonthWidget::~CMonthWidget()
{
    for (int i = 0; i < DDEMonthCalendar::MonthNumofYear; ++i) {
        CMonthRect *monthrect = m_MonthItem.at(i);
        delete  monthrect;
    }

    m_MonthItem.clear();
}

void CMonthWidget::setDate(const QDate date[12])
{
    for (int i = 0; i < DDEMonthCalendar::MonthNumofYear; ++i) {
        m_MonthItem.at(i)->setDate(date[i]);
    }

    CMonthRect::setSelectRect(m_MonthItem.at(5));
    update();
}

void CMonthWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    updateSize();
}

void CMonthWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->source() == Qt::MouseEventSynthesizedByQt) {
        //如果为触摸转换则设置触摸状态和触摸开始坐标
        m_touchState = 1;
        m_touchBeginPoint = event->pos();
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::RightButton)
        return;

    mousePress(event->pos());
}

void CMonthWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing); // 反锯齿;

    for (int i = 0; i < m_MonthItem.size(); ++i) {
        m_MonthItem.at(i)->paintItem(&painter,
                                     m_MonthItem.at(i)->rect());
    }
    painter.end();
}

void CMonthWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->source() == Qt::MouseEventSynthesizedByQt) {
        if (m_touchState == 1) {
            //如果为触摸且状态为点击则为触摸点击
            mousePress(event->pos());
            m_touchState = 0;
        }
        QWidget::mouseReleaseEvent(event);
    }
}

void CMonthWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->source() == Qt::MouseEventSynthesizedByQt) {
        QPoint currentPoint = event->pos();
        //如果移动距离大与5则为触摸移动状态
        if (QLineF(m_touchBeginPoint, currentPoint).length() > 5) {
            m_touchState = 2;
        }
        QWidget::mouseMoveEvent(event);
    }
}

void CMonthWidget::mousePress(const QPoint &point)
{
    int itemindex = getMousePosItem(point);
    if (!(itemindex < 0)) {
        if (m_MonthItem.at(itemindex)->getDate().year() < DDECalendar::QueryEarliestYear) {
            return;
        }
        CMonthRect::setSelectRect(m_MonthItem.at(itemindex));
        emit signalsSelectDate(m_MonthItem.at(itemindex)->getDate());
    }
    update();
}
/**
 * @brief CMonthWidget::updateSize  更新item大小
 */
void CMonthWidget::updateSize()
{
    qreal w = this->width() / m_MonthItem.size();
    for (int i = 0; i < m_MonthItem.size(); ++i) {
        m_MonthItem.at(i)->setRect(i * w, 0, w, this->height());
    }
    update();
}

/**
 * @brief CMonthWidget::getMousePosItem     获取鼠标点击的区域编号
 * @param pos
 * @return
 */
int CMonthWidget::getMousePosItem(const QPointF &pos)
{
    int res = -1;

    for (int i = 0 ; i < m_MonthItem.size(); ++i) {
        if (m_MonthItem.at(i)->rect().contains(pos)) {
            res = i;
            break;
        }
    }

    return res;
}


int         CMonthRect::m_themetype ;
qreal       CMonthRect::m_DevicePixelRatio;

QColor      CMonthRect::m_defaultTextColor;
QColor      CMonthRect::m_backgrounddefaultColor ;
QColor      CMonthRect::m_currentDayTextColor;
QColor      CMonthRect::m_backgroundcurrentDayColor;
QColor      CMonthRect::m_fillColor;
QFont       CMonthRect::m_dayNumFont;
CMonthRect         *CMonthRect::m_SelectRect = nullptr;

CMonthRect::CMonthRect()
{
    m_dayNumFont.setPixelSize(DDECalendar::FontSizeSixteen);
    m_dayNumFont.setWeight(QFont::Light);
}

/**
 * @brief CMonthRect::setDate   设置时间
 * @param date
 */
void CMonthRect::setDate(const QDate &date)
{
    m_Date = date;
}

/**
 * @brief CMonthRect::getDate   获取时间
 * @return
 */
QDate CMonthRect::getDate() const
{
    return  m_Date;
}

/**
 * @brief CMonthRect::rect      获取矩阵大小
 * @return
 */
QRectF CMonthRect::rect() const
{
    return  m_rect;
}

/**
 * @brief CMonthRect::setRect     设置矩阵大小
 * @param rect
 */
void CMonthRect::setRect(const QRectF &rect)
{
    m_rect = rect;
}

/**
 * @brief CMonthRect::setRect       设置矩阵大小
 * @param x
 * @param y
 * @param w
 * @param h
 */
void CMonthRect::setRect(qreal x, qreal y, qreal w, qreal h)
{
    m_rect.setRect(x, y, w, h);
}

/**
 * @brief CMonthRect::paintItem     绘制
 * @param painter
 * @param rect
 */
void CMonthRect::paintItem(QPainter *painter, const QRectF &rect)
{
    m_selectColor = CScheduleDataManage::getScheduleDataManage()->getSystemActiveColor();

    if (m_Date.year() < DDECalendar::QueryEarliestYear)
        return;
    const bool isCurrentDay = (m_Date.month() == QDate::currentDate().month()
                               && m_Date.year() == QDate::currentDate().year());

    painter->setPen(Qt::SolidLine);

    const QString dayNum = QString::number(m_Date.month());

    if (m_SelectRect == this) {
        QRectF fillRect((rect.width() - 36) / 2 + rect.x() + 6,
                        (rect.height() - 36) / 2 + 7 + rect.y(),
                        24,
                        24);
        painter->setBrush(QBrush(m_selectColor));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(fillRect);
        painter->setRenderHint(QPainter::HighQualityAntialiasing);
        painter->setPen(m_currentDayTextColor);
        painter->setFont(m_dayNumFont);
        painter->drawText(rect, Qt::AlignCenter, dayNum);
    } else {
        if (isCurrentDay) {
            painter->setPen(m_backgroundcurrentDayColor);
        } else {
            painter->setPen(m_defaultTextColor);
        }
        painter->setFont(m_dayNumFont);
        painter->drawText(rect, Qt::AlignCenter, dayNum);
    }
}

/**
 * @brief CMonthRect::setDevicePixelRatio       设置设备缩放比例
 * @param pixel
 */
void CMonthRect::setDevicePixelRatio(const qreal pixel)
{
    m_DevicePixelRatio = pixel;
}

/**
 * @brief CMonthRect::setTheMe  设置主题
 * @param type
 */
void CMonthRect::setTheMe(int type)
{
    m_themetype = type;
    QColor frameclor;

    if (type == 0 || type == 1) {
        m_defaultTextColor = Qt::black;
        m_backgrounddefaultColor = Qt::white;
        m_currentDayTextColor = Qt::white;
        m_backgroundcurrentDayColor = CScheduleDataManage::getScheduleDataManage()->getSystemActiveColor();
        m_fillColor = "#FFFFFF";
        frameclor = m_fillColor;
        m_fillColor.setAlphaF(0);
    } else if (type == 2) {
        m_defaultTextColor = "#C0C6D4";
        QColor framecolor = Qt::black;
        framecolor.setAlphaF(0.5);
        m_backgrounddefaultColor = framecolor;
        m_currentDayTextColor = "#C0C6D4";
        m_backgroundcurrentDayColor = CScheduleDataManage::getScheduleDataManage()->getSystemActiveColor();
        m_fillColor = "#FFFFFF";
        m_fillColor.setAlphaF(0.05);
        frameclor = m_fillColor;
        m_fillColor.setAlphaF(0);
    }
}

/**
 * @brief CMonthRect::setSelectRect  设置选择的矩阵
 * @param selectRect
 */
void CMonthRect::setSelectRect(CMonthRect *selectRect)
{
    m_SelectRect = selectRect;
}