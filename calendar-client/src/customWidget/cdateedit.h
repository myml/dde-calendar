/*
* Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd.
*
* Author:     chenhaifeng  <chenhaifeng@uniontech.com>
*
* Maintainer: chenhaifeng  <chenhaifeng@uniontech.com>
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
#ifndef CDATEEDIT_H
#define CDATEEDIT_H

#include <DDateEdit>

DWIDGET_USE_NAMESPACE
/**
 * @brief The CDateEdit class
 * 日期编辑框
 */

class CDateEdit : public DDateEdit
{
    Q_OBJECT
public:
    explicit CDateEdit(QWidget *parent = nullptr);
};

#endif // CDATEEDIT_H