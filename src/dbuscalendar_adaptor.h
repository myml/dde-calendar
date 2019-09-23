﻿/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp -a dbuscalendar_adaptor -c CalendarAdaptor com.deepin.Calendar.xml
 *
 * qdbusxml2cpp is Copyright (C) 2015 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#ifndef DBUSCALENDAR_ADAPTOR_H
#define DBUSCALENDAR_ADAPTOR_H

#include <QtCore/QObject>
#include <QtDBus/QtDBus>
#include "schedulestructs.h"
QT_BEGIN_NAMESPACE
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;
QT_END_NAMESPACE

/*
 * Adaptor class for interface com.deepin.Calendar
 */
class CalendarAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.Calendar")
    Q_CLASSINFO("D-Bus Introspection", ""
                "  <interface name=\"com.deepin.Calendar\">\n"
                "    <method name=\"ActiveWindow\">\n"
                "    </method>\n"
                "    <method name=\"openSchedule\">\n"
                "      <arg direction=\"in\" type=\"s\" name=\"job\"/>\n"
                "    </method>\n"
                "  </interface>\n"
                "")
public:
    CalendarAdaptor(QObject *parent);
    virtual ~CalendarAdaptor();

public: // PROPERTIES
public Q_SLOTS: // METHODS
    void ActiveWindow();
    void OpenSchedule(QString job);
Q_SIGNALS: // SIGNALS
};

#endif
