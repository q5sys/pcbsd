/**************************************************************************
*   Copyright (C) 2014 by Yuri Momotyuk                                   *
*   yurkis@pcbsd.org                                                      *
*                                                                         *
*   Permission is hereby granted, free of charge, to any person obtaining *
*   a copy of this software and associated documentation files (the       *
*   "Software"), to deal in the Software without restriction, including   *
*   without limitation the rights to use, copy, modify, merge, publish,   *
*   distribute, sublicense, and/or sell copies of the Software, and to    *
*   permit persons to whom the Software is furnished to do so, subject to *
*   the following conditions:                                             *
*                                                                         *
*   The above copyright notice and this permission notice shall be        *
*   included in all copies or substantial portions of the Software.       *
*                                                                         *
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
*   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
*   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*
*   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR     *
*   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, *
*   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR *
*   OTHER DEALINGS IN THE SOFTWARE.                                       *
***************************************************************************/

#include "cp-item.h"
#include "../config.h"
#include "pcbsd-utils.h"

#include <QSettings>
#include <QLocale>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QTemporaryFile>

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

using namespace pcbsd;

const char* const TRY_MESSAGE_FIELD = "PC-TryMessage";
const char* const TRY_COMMAND_FIELD = "PC-TryCommand";
const char* const DE_FIELD_SHOW = "OnlyShowIn";
const char* const DE_FIELD_HIDE = "NotShowIn";
const char* const KEYWORDS_FIELD= "Keywords";

const QStringList SU_NAMES = QStringList()<<QString("pc-su ")<<QString("kdesu ")<<QString("gtksu ")<<QString("sudo ")<<QString("gksu ");

const QString DEFAULT_ICON_LOCATION = PREFIX + "/share/pcbsd/pc-controlpanel/icons/";

#define OXYGEN_THEME_PATH "/usr/local/share/icons/oxygen/"
#define OXYGEN_DIMS "64x64"
#define OXYGEN OXYGEN_THEME_PATH OXYGEN_DIMS

const QString ICON_SEARCH_PATH[] =
{
    QString(PREFIX) + "/share/pcbsd/pc-controlpanel/icons/",
    OXYGEN"/apps/",
    OXYGEN"/actions/",
    OXYGEN"/categories/",
    OXYGEN"/places/",
    OXYGEN"/status/"
};
const int ICON_SEARCH_PASS_SIZE = sizeof(ICON_SEARCH_PATH) / sizeof(QString);

const char* const DEFAULT_ICON = "preferences-other.png";


///////////////////////////////////////////////////////////////////////////////
CControlPanelItem::CControlPanelItem()
{

}

///////////////////////////////////////////////////////////////////////////////
bool CControlPanelItem::read(QString file)
{
    QString Str;

    misValid= false;
    mShowIn.clear();
    mNotShowIn.clear();
    mKeywords.clear();

    //---------------Try to open file
    QSettings Reader(file, QSettings::IniFormat);
    if (Reader.status() != QSettings::NoError)
        return false;
    Reader.setIniCodec("UTF-8");

    Reader.beginGroup("Desktop Entry");

    //---------------Check TryExec field
    Str=  Reader.value("TryExec").toString();
    if (Str.length())
    {
        struct stat sts;
        if (  (0 != stat(qPrintable(Str),&sts))
            &&(0 != access(qPrintable(Str), X_OK)))
                return false;
    }

    //---------------Check TryCommand extention field
    Str= Reader.value(TRY_COMMAND_FIELD).toString();
    if (Str.length())
    {
        QTemporaryFile tmpFile;
        if (!tmpFile.open())
        {
            return false;
        }
        QTextStream out(&tmpFile);
        out<<Str.toLatin1()<<"\n";
        out.flush();
        tmpFile.flush();

        QProcess shell_proc;
        shell_proc.start("/bin/sh",QStringList()<<tmpFile.fileName());
        shell_proc.waitForFinished(7000);
        if (shell_proc.exitCode())
        {
            return false;
        }
    }//TryCommand is present

    //----------------Get Desktop environment to show (onlyShowIn)
    Str = Reader.value(DE_FIELD_SHOW).toString();
    if (Str.length())
    {
        mShowIn = Str.split(";", QString::SkipEmptyParts);
        for (int i=0; i<mShowIn.size(); i++)
        {
            mShowIn[i]= mShowIn[i].toLower().trimmed();
        }
    }// if DE_FIELD_SHOW is present

    //------------Get Desktop environment to hide (NotShowIn)
    Str = Reader.value(DE_FIELD_HIDE).toString();
    if (Str.length())
    {
        mNotShowIn = Str.split(";", QString::SkipEmptyParts);
        for (int i=0; i<mShowIn.size(); i++)
        {
            mNotShowIn[i]= mNotShowIn[i].toLower().trimmed();
        }
    }// if DE_FIELD_HIDE is present

    //--------------- Get type
    mType=Reader.value("Type").toString();

    //--------------- Get name and displayName;
    mName = Reader.value("Name").toString();
    if (!mName.length())
    {
        mName = Reader.value("GenericName").toString();
        if (!mName.length())
            return false;
    }

    mDisplayName = getLocalizedField(Reader, "Name");
    if (!mDisplayName.length())
    {
        mDisplayName = getLocalizedField(Reader, "GenericName");
    }

    //--------------- Get Exec command and check for super user rights
    mExecCommand= Reader.value("Exec").toString();
    if (mExecCommand.length())
    {
        misRootRequired = false;
        QString tmp = mExecCommand.trimmed();

        for (int i=0; i<SU_NAMES.size(); i++ )
        {
            if (tmp.left(SU_NAMES[i].size()) == SU_NAMES[i])
            {
                misRootRequired = true;
                break;
            }
        }
    }//Exec field & SU rights check

    //---------------- Get icon
    mIconFile=  Reader.value("Icon").toString();

    if (mIconFile.length())
    {
        mIcon = QIcon(mIconFile);
        if (!mIcon.availableSizes().size())
        {
            //if icon loading failed (from absolute path) try to load icon
            // from theme
            mIcon = QIcon::fromTheme(mIconFile);
        }
        if (!mIcon.availableSizes().size())
        {
            //if icon loading failed try to load one of default icons
            QString FileName = QString(DEFAULT_ICON_LOCATION) + mIconFile;
            mIcon = QIcon(FileName);
        }
        // try to find icons in some custom icon search paths
        if (!mIcon.availableSizes().size() && (mIconFile.indexOf("/") == -1))
        {
            QString icon_name = (mIconFile.indexOf(".")>0)?mIconFile:mIconFile + ".png";
            for (int i=0; i<ICON_SEARCH_PASS_SIZE; i++)
            {
                mIcon = QIcon(ICON_SEARCH_PATH[i] + icon_name);
                if (mIcon.availableSizes().size())
                    break;
            }
        }
        if (!mIcon.availableSizes().size())
        {
            // And finally set default icon
            mIcon = QIcon(QString(DEFAULT_ICON_LOCATION) + DEFAULT_ICON);
        }
    }//if got Icon field
    else
    {
        mIcon = QIcon(QString(DEFAULT_ICON_LOCATION) + DEFAULT_ICON);
    }//If Icon field is empty


    //----------------- Get TryMessage extended field
    mMsgBoxText = getLocalizedField(Reader, TRY_MESSAGE_FIELD);

    //----------------- Get keywords field
    Str = Reader.value(KEYWORDS_FIELD).toString();
    QString LocStr = getLocalizedField(Reader, KEYWORDS_FIELD);
    mKeywords = Str.split(";", QString::SkipEmptyParts);
    if (LocStr != Str)
    {
        //Append localized keywords if present
        mKeywords+= LocStr.split(";", QString::SkipEmptyParts);
    }

    mFile= file;
    misValid = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
QString CControlPanelItem::getLocalizedField(const QSettings &Reader, const QString &FieldName)
{
    QString Str;
    QString LocaleCountry = QLocale::system().name();
    QString Locale= QString("[")+LocaleCountry.left(2)+QString("]");
    LocaleCountry= QString("[")+LocaleCountry+QString("]");

    Str = Reader.value(FieldName + Locale).toString();
    if (Str.length())
        return Str;

    Str= Reader.value(FieldName+LocaleCountry).toString();
    if (Str.length())
        return Str;

    return Reader.value(FieldName).toString();
}
