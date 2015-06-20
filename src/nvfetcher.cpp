/*
  Copyright (C) 2015 Michal Kosciesza <michal@mkiol.net>

  This file is part of Kaktus.

  Kaktus is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Kaktus is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Kaktus.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <QRegExp>
#include <QtCore/qmath.h>

#include "nvfetcher.h"
#include "settings.h"
#include "downloadmanager.h"
#include "utils.h"

NvFetcher::NvFetcher(QObject *parent) :
    Fetcher(parent),
    currentJob(Idle)
{
}

void NvFetcher::signIn()
{
    data.clear();

    Settings *s = Settings::instance();

    // Check is already have cookie
    if (s->getCookie()!="") {
        prepareUploadActions();
        return;
    }

    QString password = s->getPassword();
    QString username = s->getUsername();
    QString twitterCookie = s->getTwitterCookie();
    QString authUrl = s->getAuthUrl();
    int type = s->getSigninType();

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    QString body;
    QNetworkRequest request;

    switch (type) {
    case 0:
        if (password == "" || username == "") {
            qWarning() << "Netvibes username or password is empty!";
            if (busyType == Fetcher::CheckingCredentials)
                emit errorCheckingCredentials(400);
            else
                emit error(400);
            setBusy(false);
            return;
        }
        request.setUrl(QUrl("http://www.netvibes.com/api/auth/signin"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
        body = "email="+QUrl::toPercentEncoding(username)+"&password="+QUrl::toPercentEncoding(password)+"&session_only=1";
        currentReply = nam.post(request, body.toUtf8());
        break;
    case 1:
    case 2:
        if (twitterCookie == "" || authUrl == "") {
            qWarning() << "Twitter or Facebook sign in failed!";
            if (busyType == Fetcher::CheckingCredentials)
                emit errorCheckingCredentials(400);
            else
                emit error(400);
            setBusy(false);
            return;
        }
        request.setUrl(QUrl(authUrl));
        setCookie(request, twitterCookie);
        currentReply = nam.get(request);
        break;
    default:
        qWarning() << "Invalid sign in type!";
        emit error(500);
        setBusy(false);
        return;
    }

    if (busyType == Fetcher::CheckingCredentials)
        connect(currentReply, SIGNAL(finished()), this, SLOT(finishedSignInOnlyCheck()));
    else
        connect(currentReply, SIGNAL(finished()), this, SLOT(finishedSignIn()));

    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::startFetching()
{
    Settings *s = Settings::instance();

    storedStreamList = s->db->readStreamModuleTabListWithoutDate();

    s->db->cleanDashboards();
    s->db->cleanTabs();
    s->db->cleanModules();

    // Create Cache structure
    if(busyType == Fetcher::Initiating) {
        s->db->cleanCache();
    }

    fetchDashboards();
}

void NvFetcher::fetchDashboards()
{
    data.clear();

    Settings *s = Settings::instance();

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    QUrl url("http://www.netvibes.com/api/my/dashboards");
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());
    currentReply = nam.post(request,"format=json");
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedDashboards()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::fetchTabs()
{
    data.clear();

    Settings *s = Settings::instance();

    QString dashbordId = dashboardList.first();

    QUrl url("http://www.netvibes.com/api/my/dashboards/data");
    QNetworkRequest request(url);

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());
    currentReply = nam.post(request,"format=json&pageId="+dashbordId.toUtf8());
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedTabs()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::fetchFeeds()
{
    data.clear();

    Settings *s = Settings::instance();

    QUrl url("http://www.netvibes.com/api/streams");
    QNetworkRequest request(url);

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());

    int feedsAtOnce = s->getFeedsAtOnce();

    int ii = 0;
    QString actions = "[";
    QList<DatabaseManager::StreamModuleTab>::iterator i = streamList.begin();
    while (i != streamList.end()) {
        if (ii > feedsAtOnce)
            break;

        if (ii != 0)
            actions += ",";

        actions += QString("{\"options\":{\"limit\":%1},\"streams\":[{\"id\":\"%2\",\"moduleId\":\"%3\"}]}")
                .arg(limitFeeds)
                .arg((*i).streamId)
                .arg((*i).moduleId);

        //qDebug() << "streamId:" << (*i).streamId << "moduleId:" << (*i).moduleId;

        i = streamList.erase(i);
        ++ii;
    }
    actions += "]";

    QString content = "actions="+QUrl::toPercentEncoding(actions)+"&pageId="+s->getDashboardInUse();

    currentReply = nam.post(request, content.toUtf8());
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedFeeds()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::fetchFeedsReadlater()
{
    data.clear();

    Settings *s = Settings::instance();

    QUrl url("http://www.netvibes.com/api/streams/saved");
    QNetworkRequest request(url);

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());

    QString actions;
    if (publishedBeforeDate==0) {
        actions = QString("[{\"options\":{\"limit\":%1}}]").arg(limitFeedsReadlater);
    } else {
        actions = QString("[{\"options\":{\"limit\":%1, "
                          "\"publishedBeforeDate\":%2"
                          "}}]")
                .arg(limitFeedsReadlater)
                .arg(publishedBeforeDate);
    }

    QString content = "actions="+QUrl::toPercentEncoding(actions)+"&pageId="+s->getDashboardInUse();

    currentReply = nam.post(request, content.toUtf8());
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedFeedsReadlater()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::fetchFeedsUpdate()
{
    data.clear();

    Settings *s = Settings::instance();

    QUrl url("http://www.netvibes.com/api/streams");
    QNetworkRequest request(url);

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());

    int ii = 0;
    QString actions = "[";
    QList<DatabaseManager::StreamModuleTab>::iterator i = streamUpdateList.begin();
    while (i != streamUpdateList.end()) {
        if (ii >= feedsUpdateAtOnce)
            break;

        if (ii != 0)
            actions += ",";

        actions += QString("{\"options\":{\"limit\":%1},\"crawledAfterDate\":%2,"
                           "\"streams\":[{\"id\":\"%3\",\"moduleId\":\"%4\"}]}")
                .arg(limitFeedsUpdate)
                .arg((*i).date)
                .arg((*i).streamId)
                .arg((*i).moduleId);

        i = streamUpdateList.erase(i);
        ++ii;
    }
    actions += "]";

    QString content = "actions="+QUrl::toPercentEncoding(actions)+"&pageId="+s->getDashboardInUse();

    currentReply = nam.post(request, content.toUtf8());
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedFeedsUpdate()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::setAction()
{
    data.clear();

    Settings *s = Settings::instance();
    DatabaseManager::Action action = actionsList.first();

    QUrl url;

    switch (action.type) {
    case DatabaseManager::SetRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/add");
        break;
    case DatabaseManager::SetSaved:
        url.setUrl("http://www.netvibes.com/api/streams/saved/add");
        break;
    case DatabaseManager::UnSetRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/remove");
        break;
    case DatabaseManager::UnSetSaved:
        url.setUrl("http://www.netvibes.com/api/streams/saved/remove");
        break;
    case DatabaseManager::UnSetStreamReadAll:
        url.setUrl("http://www.netvibes.com/api/streams/read/remove");
        break;
    case DatabaseManager::SetStreamReadAll:
        url.setUrl("http://www.netvibes.com/api/streams/read/add");
        break;
    case DatabaseManager::UnSetTabReadAll:
        url.setUrl("http://www.netvibes.com/api/streams/read/remove");
        break;
    case DatabaseManager::SetTabReadAll:
        url.setUrl("http://www.netvibes.com/api/streams/read/add");
        break;
    case DatabaseManager::UnSetAllRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/remove");
        break;
    case DatabaseManager::SetAllRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/add");
        break;
    case DatabaseManager::UnSetSlowRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/remove");
        break;
    case DatabaseManager::SetSlowRead:
        url.setUrl("http://www.netvibes.com/api/streams/read/add");
        break;
    }

    QNetworkRequest request(url);

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
    setCookie(request, s->getCookie().toLatin1());

    QString actions = "[";

    if (action.type == DatabaseManager::SetTabReadAll ||
            action.type == DatabaseManager::UnSetTabReadAll) {

        QList<DatabaseManager::StreamModuleTab> list = s->db->readStreamModuleTabListByTab(action.id1);

        if (list.empty()) {
            qWarning() << "Action is broken!";
            removeAction();
            return;
        }

        int lastPublishedAt = s->db->readLastPublishedAtByTab(action.id1)+1;

        actions += QString("{\"options\":{\"publishedBeforeDate\":%1},\"streams\":[").arg(lastPublishedAt);

        QList<DatabaseManager::StreamModuleTab>::iterator i = list.begin();
        while (i != list.end()) {
            if (i != list.begin())
                actions  += ",";

            actions += QString("{\"id\":\"%1\",\"moduleId\":\"%2\"}").arg((*i).streamId).arg((*i).moduleId);

            ++i;
        }

        actions += "]}";
    }

    if (action.type == DatabaseManager::SetAllRead ||
            action.type == DatabaseManager::UnSetAllRead) {

        QList<DatabaseManager::StreamModuleTab> list = s->db->readStreamModuleTabListByDashboard(s->getDashboardInUse());

        if (list.empty()) {
            qWarning() << "Action is broken!";
            removeAction();
            return;
        }

        int lastPublishedAt = s->db->readLastPublishedAtByDashboard(s->getDashboardInUse())+1;

        actions += QString("{\"options\":{\"publishedBeforeDate\":%1},\"streams\":[").arg(lastPublishedAt);

        QList<DatabaseManager::StreamModuleTab>::iterator i = list.begin();
        while (i != list.end()) {
            if (i != list.begin())
                actions  += ",";

            actions += QString("{\"id\":\"%1\",\"moduleId\":\"%2\"}").arg((*i).streamId).arg((*i).moduleId);

            ++i;
        }

        actions += "]}";
    }

    if (action.type == DatabaseManager::SetSlowRead ||
            action.type == DatabaseManager::UnSetSlowRead) {

        QList<DatabaseManager::StreamModuleTab> list = s->db->readSlowStreamModuleTabListByDashboard(s->getDashboardInUse());

        if (list.empty()) {
            qWarning() << "Action is broken!";
            removeAction();
            return;
        }

        int lastPublishedAt = s->db->readLastPublishedAtSlowByDashboard(s->getDashboardInUse())+1;

        actions += QString("{\"options\":{\"publishedBeforeDate\":%1},\"streams\":[").arg(lastPublishedAt);

        QList<DatabaseManager::StreamModuleTab>::iterator i = list.begin();
        while (i != list.end()) {
            if (i != list.begin())
                actions  += ",";

            actions += QString("{\"id\":\"%1\",\"moduleId\":\"%2\"}").arg((*i).streamId).arg((*i).moduleId);

            ++i;
        }

        actions += "]}";
    }

    if (action.type == DatabaseManager::SetStreamReadAll ||
            action.type == DatabaseManager::UnSetStreamReadAll) {

        int lastPublishedAt = s->db->readLastPublishedAtByStream(action.id1)+1;

        actions += QString("{\"options\":{\"publishedBeforeDate\":%1},\"streams\":[").arg(lastPublishedAt);
        actions += QString("{\"id\":\"%1\"}").arg(action.id1);
        actions += "]}";
    }

    if (action.type == DatabaseManager::SetRead ||
            action.type == DatabaseManager::UnSetRead ||
            action.type == DatabaseManager::SetSaved ||
            action.type == DatabaseManager::UnSetSaved ) {

        if (action.date1==0) {
            qWarning() << "PublishedAt date is 0!";
        }

        actions += QString("{\"streams\":[{\"id\":\"%1\",\"items\":[{"
                           "\"id\":\"%2\",\"publishedAt\":%3}]}]}")
                .arg(action.id2).arg(action.id1).arg(action.date1);
    }

    actions += "]";

    //qDebug() << "action.type="<<action.type<<"actions=" << actions;
    QString content = "actions="+QUrl::toPercentEncoding(actions)+"&pageId="+s->getDashboardInUse();
    //qDebug() << "content=" << content;

    currentReply = nam.post(request, content.toUtf8());
    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedSetAction()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}


bool NvFetcher::setConnectUrl(const QString &url)
{
    if (QUrl(url).path()=="/connect/facebook") {
        Settings *s = Settings::instance();
        s->setAuthUrl(url);
        return true;
    }

    if (QUrl(url).path()=="/connect/twitter") {
        Settings *s = Settings::instance();
        /*QList< QPair<QString, QString> > list = _url.queryItems();
        QList< QPair<QString, QString> >::iterator i = list.begin();
        while (i != list.end()) {
           QPair<QString, QString> pair = *i;
           if (pair.first=="oauth_token")
               s->setTwitterToken(pair.second);
           else if (pair.first=="oauth_verifier")
               s->setTwitterVerifier(pair.second);
           ++i;
        }*/
        s->setAuthUrl(url);
        return true;
    }
    return false;
}

void NvFetcher::getConnectUrl(int type)
{
    if (busy) {
        qWarning() << "Fetcher is busy!";
        return;
    }

    data.clear();

    if (currentReply != NULL) {
        currentReply->disconnect();
        currentReply->deleteLater();
        currentReply = NULL;
    }

    QNetworkRequest request;

    switch (type) {
    case 1:
        request.setUrl(QUrl("http://www.netvibes.com/api/auth/get-url"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
        currentReply = nam.post(request,"callbackUrl=%2Fconnect%2Ftwitter&service=twitter");
        break;
    case 2:
        request.setUrl(QUrl("http://www.netvibes.com/api/auth/get-url"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded; charset=UTF-8");
        currentReply = nam.post(request,"callbackUrl=%2Fconnect%2Ffacebook&service=facebook");
        break;
    default:
        qWarning() << "Wrong sign in type!";
        return;
    }

    setBusy(true, Fetcher::GettingAuthUrl);

    connect(currentReply, SIGNAL(finished()), this, SLOT(finishedGetAuthUrl()));
    connect(currentReply, SIGNAL(readyRead()), this, SLOT(readyRead()));
    connect(currentReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(networkError(QNetworkReply::NetworkError)));
}

void NvFetcher::finishedGetAuthUrl()
{
    if (currentReply != NULL && currentReply->error()) {
        qWarning() << "Error while getting authentication URL!";
        setBusy(false);
        emit errorGettingAuthUrl();
        return;
    }

    Settings *s = Settings::instance();

    if (parse()) {
        if (jsonObj["success"].toBool()) {

            QString url = jsonObj["url"].toString();

            if (url == "") {
                qWarning() << "Authentication URL is empty!";
                setBusy(false);
                emit errorGettingAuthUrl();
                return;
            }

            QNetworkReply* reply = dynamic_cast<QNetworkReply*>(sender());
            s->setTwitterCookie(QString(reply->rawHeader("Set-Cookie")));

            //QVariant setCookieHeader = reply->header(QNetworkRequest::SetCookieHeader);
            /*if (setCookieHeader.type()==QVariant::List) {
                QList<QNetworkCookie> list = setCookieHeader.toL;
                QList<QNetworkCookie>::iterator i = list.begin();
                while (i != list.end()) {
                    qDebug() << static_cast<QNetworkCookie>(*i).toRawForm(QNetworkCookie::Full);
                    ++i;
                }
            }*/

            setBusy(false);

            if (url.contains("twitter"))
                emit newAuthUrl(url,1);
            else if (url.contains("facebook"))
                emit newAuthUrl(url,2);

            return;
        }
    }

    qWarning() << "Can not get authentication URL!";
    setBusy(false);
    emit errorGettingAuthUrl();
}

void NvFetcher::finishedSignIn()
{
    //qDebug() << this->_data;

    Settings *s = Settings::instance();

    if (currentReply->error() &&
        currentReply->error()!=QNetworkReply::OperationCanceledError) {
        if (s->getSigninType()>0) {
            qWarning() << "Sign in with social service failed!";
            emit error(403);
            setBusy(false);
            return;
        }

        qWarning() << "Sign in failed!";
        emit error(501);
        setBusy(false);
        return;
    }

    QString cookie(currentReply->rawHeader("Set-Cookie"));

    switch (s->getSigninType()) {
    case 0:
        if (parse()) {
            if (jsonObj["success"].toBool()) {
                s->setSignedIn(true);
                s->setCookie(cookie);
                prepareUploadActions();
            } else {
                s->setSignedIn(false);

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                QString message = jsonObj["error"].toObject()["message"].toString();
#else
                QString message = jsonObj["error"].toMap()["message"].toString();
#endif
                if (message == "no match")
                    emit error(402);
                else
                    emit error(401);
                setBusy(false);
                qWarning() << "Sign in failed!" << "Messsage: " << message;
            }
        } else {
            s->setSignedIn(false);
            qWarning() << "Sign in failed!";
            emit error(501);
            setBusy(false);
        }
        break;
    case 1:
    case 2:
        if (!checkCookie(cookie)) {
            s->setSignedIn(false);
            qWarning() << "Sign in failed!";
            emit error(501);
            setBusy(false);
            return;
        }

        s->setCookie(cookie);
        s->setSignedIn(true);
        prepareUploadActions();

        break;
    default:
        qWarning() << "Invalid sign in type!";
        emit error(501);
        setBusy(false);
        s->setSignedIn(false);
        return;
    }
}

void NvFetcher::finishedSignInOnlyCheck()
{
    //qDebug() << this->_data;
    if (currentReply->error() &&
        currentReply->error()!=QNetworkReply::OperationCanceledError) {
        qWarning() << "Sign in failed!";
        emit errorCheckingCredentials(501);
        setBusy(false);
        return;
    }

    Settings *s = Settings::instance();

    QString cookie(currentReply->rawHeader("Set-Cookie"));

    switch (s->getSigninType()) {
    case 0:
        if (parse()) {
            if (jsonObj["success"].toBool()) {
                s->setSignedIn(true);
                s->setCookie(cookie);
                emit credentialsValid();
                setBusy(false);
            } else {
                s->setSignedIn(false);
                QString message = jsonObj["message"].toString();
                if (message == "nomatch")
                    emit errorCheckingCredentials(402);
                else
                    emit errorCheckingCredentials(401);
                setBusy(false);
                qWarning() << "Sign in check failed!" << "Messsage: " << message;
            }
        } else {
            s->setSignedIn(false);
            qWarning() << "Sign in check failed!";
            emit errorCheckingCredentials(501);
            setBusy(false);
        }
        break;
    case 1:
    case 2:
        if (!checkCookie(cookie)) {
            s->setSignedIn(false);
            qWarning() << "Sign in check failed!";
            emit errorCheckingCredentials(501);
            setBusy(false);
            return;
        }
        s->setCookie(cookie);
        s->setSignedIn(true);
        emit credentialsValid();
        setBusy(false);
        break;
    default:
        qWarning() << "Invalid sign in type!";
        emit errorCheckingCredentials(501);
        setBusy(false);
        s->setSignedIn(false);
        return;
    }
}

void NvFetcher::finishedDashboards()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    startJob(StoreDashboards);
}

void NvFetcher::finishedDashboards2()
{
    Settings *s = Settings::instance();

    if(!dashboardList.isEmpty()) {
        fetchTabs();
    } else {
        qWarning() << "No Dashboards found!";
        taskEnd();
    }
}

void NvFetcher::finishedTabs()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    startJob(StoreTabs);
}

void NvFetcher::finishedTabs2()
{
    Settings *s = Settings::instance();

    if(!dashboardList.isEmpty()) {
        fetchTabs();
    } else {
        if (tabList.isEmpty()) {
            qWarning() << "No Tabs!";
        }
        if (streamList.isEmpty()) {
            qWarning() << "No Streams!";
            taskEnd();
        } else {
            if (busyType == Fetcher::Updating) {
                // Set current entries as not fresh
                s->db->updateEntriesFreshFlag(0);

                streamUpdateList = s->db->readStreamModuleTabList();

                cleanRemovedFeeds();
                cleanNewFeeds();

                s->db->cleanStreams();

                if (streamList.isEmpty()) {
                    qDebug() << "No new Feeds!";
                    proggressTotal = qCeil(streamUpdateList.count()/feedsUpdateAtOnce)+3;
                    emit progress(3,proggressTotal);
                    fetchFeedsUpdate();
                } else {
                    proggressTotal = qCeil(streamUpdateList.count()/feedsUpdateAtOnce)+qCeil(streamList.count()/feedsAtOnce)+3;
                    emit progress(3,proggressTotal);
                    fetchFeeds();
                }
            }

            if (busyType == Fetcher::Initiating) {
                s->db->cleanStreams();
                s->db->cleanEntries();
                //s->db->cleanCache();

                proggressTotal = qCeil(streamList.count()/feedsAtOnce)+3;
                emit progress(3,proggressTotal);
                fetchFeeds();
            }
        }
    }
}

void NvFetcher::finishedFeeds()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    startJob(StoreFeeds);
}

void NvFetcher::finishedFeeds2()
{
    Settings *s = Settings::instance();

    emit progress(proggressTotal-((streamList.count()/feedsAtOnce)+(streamUpdateList.count()/feedsUpdateAtOnce)),proggressTotal);

    if (streamList.isEmpty()) {

        if(busyType == Fetcher::Updating) {
            streamUpdateList = s->db->readStreamModuleTabList();
            fetchFeedsUpdate();
        }

        if(busyType == Fetcher::Initiating) {
            publishedBeforeDate = 0;
            fetchFeedsReadlater();
        }

    } else {
        fetchFeeds();
    }
}

void NvFetcher::finishedFeedsReadlater()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    publishedBeforeDate = 0;
    startJob(StoreFeedsReadlater);
}

void NvFetcher::finishedFeedsReadlater2()
{
    if (publishedBeforeDate!=0) {
        fetchFeedsReadlater();
    } else {
        // Fix for very old entries. Mark as unsaved entries unmarked on server
        Settings *s = Settings::instance();
        s->db->updateEntriesSavedFlagByFlagAndDashboard(s->getDashboardInUse(),9,0);

        dashboardList.clear();
        tabList.clear();
        streamList.clear();
        streamUpdateList.clear();
        storedStreamList.clear();

        taskEnd();
    }
}

void NvFetcher::finishedFeedsUpdate()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    startJob(StoreFeedsUpdate);
}

void NvFetcher::finishedFeedsUpdate2()
{
    Settings *s = Settings::instance();

    emit progress(proggressTotal-qCeil(streamUpdateList.count()/feedsUpdateAtOnce),proggressTotal);

    if (streamUpdateList.isEmpty()) {
        // Fetching Saved items
        publishedBeforeDate = 0;
        s->db->updateEntriesSavedFlagByFlagAndDashboard(s->getDashboardInUse(),1,9);
        fetchFeedsReadlater();
    } else {
        fetchFeedsUpdate();
    }
}

void NvFetcher::finishedSetAction()
{
    //qDebug() << data;
    if (currentReply->error()) {
        emit error(500);
        setBusy(false);
        return;
    }

    if(!parse()) {
        qWarning() << "Error parsing Json!";
        emit error(600);
        setBusy(false);
        return;
    }

    checkError();

    Settings *s = Settings::instance();

    // Deleting action
    DatabaseManager::Action action = actionsList.takeFirst();
    s->db->removeActionsById(action.id1);

    if (actionsList.isEmpty()) {
        s->db->cleanDashboards();
        startFetching();
    } else {
        uploadActions();
    }
}

void NvFetcher::run()
{
    int count = 0;
    switch (currentJob) {
    case StoreDashboards:
        storeDashboards();
        break;
    case StoreTabs:
        storeTabs();
        break;
    case StoreFeeds:
    case StoreFeedsUpdate:
        storeFeeds();
        break;
    case StoreFeedsReadlater:
        count = storeFeeds();
        if (count<limitFeedsReadlater) {
            publishedBeforeDate = 0;
        }
        break;
    default:
        qWarning() << "Unknown Job!";
        break;
    }
}

void NvFetcher::startJob(Job job)
{
    if (isRunning()) {
        qWarning() << "Job is running";
        return;
    }

    disconnect(this, SIGNAL(finished()), 0, 0);
    currentJob = job;

    if(parse()) {
        if (jsonObj.contains("success") && !jsonObj["success"].toBool()) {

            qWarning() << "Cookie expires!";
            Settings *s = Settings::instance();
            s->setCookie("");
            setBusy(false);

            // If credentials other than Netvibes, prompting for re-auth
            if (s->getSigninType()>0) {
                emit error(403);
                return;
            }

            update();
            return;
        }
    } else {
        qWarning() << "Error parsing Json!";
        emit error(600);
        setBusy(false);
        return;
    }

    switch (job) {
    case StoreDashboards:
        connect(this, SIGNAL(finished()), this, SLOT(finishedDashboards2()));
        break;
    case StoreTabs:
        connect(this, SIGNAL(finished()), this, SLOT(finishedTabs2()));
        break;
    case StoreFeeds:
        connect(this, SIGNAL(finished()), this, SLOT(finishedFeeds2()));
        break;
    case StoreFeedsInfo:
        connect(this, SIGNAL(finished()), this, SLOT(finishedFeedsInfo2()));
        break;
    case StoreFeedsUpdate:
        connect(this, SIGNAL(finished()), this, SLOT(finishedFeedsUpdate2()));
        break;
    case StoreFeedsReadlater:
        connect(this, SIGNAL(finished()), this, SLOT(finishedFeedsReadlater2()));
        break;
    default:
        qWarning() << "Unknown Job!";
        emit error(502);
        setBusy(false);
        return;
    }

    start(QThread::LowPriority);
}

void NvFetcher::storeDashboards()
{
    if (checkError()) {
        return;
    }

    Settings *s = Settings::instance();

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if (jsonObj["dashboards"].isObject()) {
#else
    if (jsonObj["dashboards"].type()==QVariant::Map) {
#endif
        // Set default dashboard if not set
        QString defaultDashboardId = s->getDashboardInUse();
        int lowestDashboardId = 99999999;
        bool defaultDashboardIdExists = false;
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
        QJsonObject::const_iterator i = jsonObj["dashboards"].toObject().constBegin();
        QJsonObject::const_iterator end = jsonObj["dashboards"].toObject().constEnd();
#else
        QVariantMap::const_iterator i = jsonObj["dashboards"].toMap().constBegin();
        QVariantMap::const_iterator end = jsonObj["dashboards"].toMap().constEnd();
#endif
        while (i != end) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            QJsonObject obj = i.value().toObject();
#else
            QVariantMap obj = i.value().toMap();
#endif
            if (obj["active"].toString()=="1") {

                DatabaseManager::Dashboard d;
                d.id = obj["pageId"].toString();
                d.name = obj["name"].toString();
                d.title = obj["title"].toString();
                d.description = obj["description"].toString();
                s->db->writeDashboard(d);
                dashboardList.append(d.id);
                //qDebug() << "Writing dashboard: " << d.title;

                // Search lowest id
                int iid = d.id.toInt();
                if (iid < lowestDashboardId)
                    lowestDashboardId = iid;
                if (defaultDashboardId == d.id)
                    defaultDashboardIdExists = true;
            }

            ++i;
        }

        // Set default dashboard if not set
        if (defaultDashboardId=="" || defaultDashboardIdExists==false) {
            s->setDashboardInUse(QString::number(lowestDashboardId));
        }

    } else {
        qWarning() << "No dashboards element found!";
    }
}

void NvFetcher::storeTabs()
{
    if (checkError()) {
        return;
    }

    Settings *s = Settings::instance();
    QString dashboardId = dashboardList.takeFirst();

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if (jsonObj["userData"].toObject()["tabs"].isArray()) {
        QJsonArray::const_iterator i = jsonObj["userData"].toObject()["tabs"].toArray().constBegin();
        QJsonArray::const_iterator end = jsonObj["userData"].toObject()["tabs"].toArray().constEnd();
#else
    if (jsonObj["userData"].toMap()["tabs"].type()==QVariant::List) {
        QVariantList::const_iterator i = jsonObj["userData"].toMap()["tabs"].toList().constBegin();
        QVariantList::const_iterator end = jsonObj["userData"].toMap()["tabs"].toList().constEnd();
#endif
        while (i != end) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            QJsonObject obj = (*i).toObject();
#else
            QVariantMap obj = (*i).toMap();
#endif
            DatabaseManager::Tab t;
            t.id = obj["id"].toString();
            t.dashboardId = dashboardId;
            t.title = obj["title"].toString();

            // tab icon detection
            bool doDownloadIcon = true;
            t.icon = obj["icon"].toString();

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            if (t.icon=="" && obj["iconConfig"].isObject()) {
                doDownloadIcon = false;
                QJsonObject iconObj = obj["iconConfig"].toObject();
                t.icon = QString("image://nvicons/%1?%2").arg(iconObj["icon"].toString()).arg(iconObj["color"].toString());
            }
#else
            if (t.icon=="" && obj["iconConfig"].type()==QVariant::Map) {
                doDownloadIcon = false;
                QVariantMap iconObj = obj["iconConfig"].toMap();
                t.icon = QString("image://nvicons/%1?%2").arg(iconObj["icon"].toString()).arg(iconObj["color"].toString());
            }
#endif

            s->db->writeTab(t);
            tabList.append(t.id);

            //qDebug() << "Writing tab: " << t.id << t.title;

            // Downloading icon file
            if (doDownloadIcon && t.icon!="") {
                DatabaseManager::CacheItem item;
                item.origUrl = t.icon;
                item.finalUrl = t.icon;
                item.type = "icon";
                emit addDownload(item);
                //qDebug() << "icon:" << t.icon;
            }

            ++i;
        }
    }  else {
        qWarning() << "No \"tabs\" element found!";
    }

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if (jsonObj["userData"].toObject()["modules"].isArray()) {
        QJsonArray::const_iterator i = jsonObj["userData"].toObject()["modules"].toArray().constBegin();
        QJsonArray::const_iterator end = jsonObj["userData"].toObject()["modules"].toArray().constEnd();
#else
    if (jsonObj["userData"].toMap()["modules"].type()==QVariant::List) {
        QVariantList::const_iterator i = jsonObj["userData"].toMap()["modules"].toList().constBegin();
        QVariantList::const_iterator end = jsonObj["userData"].toMap()["modules"].toList().constEnd();
#endif
        while (i != end) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            QJsonObject obj = (*i).toObject();
#else
            QVariantMap obj = (*i).toMap();
#endif

            if (obj["name"].toString() == "RssReader" ||
                    obj["name"].toString() == "MultipleFeeds") {

                // Module
                DatabaseManager::Module m;
                m.id = obj["id"].toString();
                m.name = obj["name"].toString();
                m.title = obj["title"].toString();
                m.status = obj["status"].toString();
                m.widgetId = obj["widgetId"].toString();
                m.pageId = obj["pageId"].toString();
                m.tabId = obj["tab"].toString();

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                if (obj["streams"].isArray()) {
                    QJsonArray::const_iterator mi = obj["streams"].toArray().constBegin();
                    QJsonArray::const_iterator mend = obj["streams"].toArray().constEnd();
                    while (mi != mend) {
                        QJsonObject mobj = (*mi).toObject();
#else
                if (obj["streams"].type()==QVariant::List) {
                    QVariantList::const_iterator mi = obj["streams"].toList().constBegin();
                    QVariantList::const_iterator mend = obj["streams"].toList().constEnd();
                    while (mi != mend) {
                        QVariantMap mobj = (*mi).toMap();
#endif
                        DatabaseManager::StreamModuleTab smt;
                        smt.streamId = mobj["id"].toString();
                        smt.moduleId = m.id;
                        smt.tabId = obj["tab"].toString();
                        streamList.append(smt);
                        m.streamList.append(smt.streamId);
                        //qDebug() << "Writing module: " << "tabid:" << smt.tabId << "moduleId:" << smt.moduleId << "streamId:" << smt.streamId << m.title;
                        ++mi;
                    }
                } else {
                    qWarning() << "Module"<<m.id<<"without streams!";
                }


                s->db->writeModule(m);
            }

            ++i;
        }
    }  else {
        qWarning() << "No modules element found!";
    }
}

int NvFetcher::storeFeeds()
{
    if (checkError()) {
        return 0;
    }

    Settings *s = Settings::instance();
    int entriesCount = 0;

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if (jsonObj["results"].isArray()) {
        QJsonArray::const_iterator i = jsonObj["results"].toArray().constBegin();
        QJsonArray::const_iterator end = jsonObj["results"].toArray().constEnd();
#else
    if (jsonObj["results"].type()==QVariant::List) {
        QVariantList::const_iterator i = jsonObj["results"].toList().constBegin();
        QVariantList::const_iterator end = jsonObj["results"].toList().constEnd();
#endif
        while (i != end) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
            if ((*i).isObject()) {
                if ((*i).toObject()["streams"].isArray()) {
                    QJsonArray::const_iterator ai = (*i).toObject()["streams"].toArray().constBegin();
                    QJsonArray::const_iterator aend = (*i).toObject()["streams"].toArray().constEnd();
#else
            if ((*i).type()==QVariant::Map) {
                if ((*i).toMap()["streams"].type()==QVariant::List) {
                    QVariantList::const_iterator ai = (*i).toMap()["streams"].toList().constBegin();
                    QVariantList::const_iterator aend = (*i).toMap()["streams"].toList().constEnd();
#endif
                    while (ai != aend) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        QJsonObject obj = (*ai).toObject();
                        if (obj["error"].isObject()) {
                            int code = (int) obj["error"].toObject()["code"].toDouble();
                            if (code != 204) {
                                qWarning() << "Nested error in Netvibes response!";
                                qWarning() << "Code:" << code;
                                qWarning() << "Message:" << obj["error"].toObject()["message"].toString();
                                qWarning() << "JSON obj:" << obj;
                                qWarning() << "id:" << obj["id"].toString();
                            }
                        }
#else
                        QVariantMap obj = (*ai).toMap();
                        if (obj["error"].type()==QVariant::Map) {
                            qWarning() << "Nested error in Netvibes response!";
                            qWarning() << "Code:" << (int) obj["error"].toMap()["code"].toDouble();
                            qWarning() << "Message:" << obj["error"].toMap()["message"].toString();
                            qWarning() << "JSON obj:" << obj;
                            qWarning() << "id:" << obj["id"].toString();
                        }
#endif
                        int slow = 0;
                        if (obj.contains("slow"))
                            slow = obj["slow"].toBool() ? 1 : 0;

                        DatabaseManager::Stream st;
                        st.id = obj["id"].toString();
                        st.title = obj["title"].toString().remove(QRegExp("<[^>]*>"));
                        st.link = obj["link"].toString();
                        st.query = obj["query"].toString();
                        st.content = obj["content"].toString();
                        st.type = obj["type"].toString();
                        st.unread = 0;
                        st.read = 0;
                        st.slow = slow;
                        st.newestItemAddedAt = (int) obj["newestItemAddedAt"].toDouble();
                        st.updateAt = (int) obj["updateAt"].toDouble();
                        st.lastUpdate = QDateTime::currentDateTimeUtc().toTime_t();

                        // Downloading fav icon file
                        if (st.link!="") {
                            QUrl iconUrl(st.link);
                            st.icon = QString("http://avatars.netvibes.com/favicon/%1://%2")
                                    .arg(iconUrl.scheme())
                                    .arg(iconUrl.host());
                            DatabaseManager::CacheItem item;
                            item.origUrl = st.icon;
                            item.finalUrl = st.icon;
                            item.type = "icon";
                            emit addDownload(item);
                        }

                        //qDebug() << "Writing Stream: " << st.id << st.title;
                        s->db->writeStream(st);
                        ++ai;
                    }
                } else {
                    qWarning() << "No \"streams\" element found!";
                }
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                if ((*i).toObject()["items"].isArray()) {
                    QJsonArray::const_iterator ai = (*i).toObject()["items"].toArray().constBegin();
                    QJsonArray::const_iterator aend = (*i).toObject()["items"].toArray().constEnd();
#else

                if ((*i).toMap()["items"].type()==QVariant::List) {
                    QVariantList::const_iterator ai = (*i).toMap()["items"].toList().constBegin();
                    QVariantList::const_iterator aend = (*i).toMap()["items"].toList().constEnd();
#endif
                    while (ai != aend) {
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        QJsonObject obj = (*ai).toObject();
#else
                        QVariantMap obj = (*ai).toMap();
#endif
                        int read = 1; int saved = 0;
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        if (obj["flags"].isObject()) {

                            if (!obj["flags"].toObject().contains("read")) {
                                read = 2;
                            } else {
                                read = obj["flags"].toObject()["read"].toBool() ? 1 : 0;
                            }

                            if (!obj["flags"].toObject().contains("saved")) {
                                saved = 0;
                            } else {
                                saved = obj["flags"].toObject()["saved"].toBool() ? 1 : 0;
                            }
                        }
#else
                        if (obj["flags"].type()==QVariant::Map) {

                            if (!obj["flags"].toMap().contains("read")) {
                                read = 2;
                            } else {
                                read = obj["flags"].toMap()["read"].toBool() ? 1 : 0;
                            }

                            if (!obj["flags"].toMap().contains("saved")) {
                                saved = 0;
                            } else {
                                saved = obj["flags"].toMap()["saved"].toBool() ? 1 : 0;
                            }
                        }
#endif
                        QString image = "";
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        if (obj["enclosures"].isArray()) {
                            if (!obj["enclosures"].toArray().empty()) {
                                QString link = obj["enclosures"].toArray()[0].toObject()["link"].toString();
                                QString type = obj["enclosures"].toArray()[0].toObject()["type"].toString();
#else
                        if (obj["enclosures"].type()==QVariant::List) {
                            if (!obj["enclosures"].toList().isEmpty()) {
                                QString link = obj["enclosures"].toList()[0].toMap()["link"].toString();
                                QString type = obj["enclosures"].toList()[0].toMap()["type"].toString();
#endif
                                if (type=="image"||type=="html")
                                    image = link;
                            }
                        }

                        QString author = "";
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        if (obj["authors"].isArray()) {
                            if (!obj["authors"].toArray().empty())
                                author = obj["authors"].toArray()[0].toObject()["name"].toString();
                        }
#else
                        if (obj["authors"].type()==QVariant::List) {
                            if (!obj["authors"].toList().isEmpty())
                                author = obj["authors"].toList()[0].toMap()["name"].toString();
                        }
#endif
                        DatabaseManager::Entry e;
                        e.id = obj["id"].toString();
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
                        e.streamId = obj["stream"].toObject()["id"].toString();
#else
                        e.streamId = obj["stream"].toMap()["id"].toString();
#endif
                        //e.title = obj["title"].toString().remove(QRegExp("<[^>]*>"));
                        e.title = obj["title"].toString();
                        e.author = author;
                        e.link = obj["link"].toString();
                        e.image = image;
                        e.content = obj["content"].toString();
                        e.read = read;
                        e.saved = saved;
                        e.cached = 0;
                        e.publishedAt = obj["publishedAt"].toDouble();
                        e.createdAt = obj["createdAt"].toDouble();
                        e.fresh = 1;

                        // Downloading image file
                        if (s->getCachingMode() == 2 || (s->getCachingMode() == 1 && s->dm->isWLANConnected())) {
                            if (image!="") {
                                //qDebug() << "netvibes image:" << image;
                                // Image provided by Netvibes API :-)
                                if (!s->db->isCacheExistsByFinalUrl(Utils::hash(image))) {
                                    DatabaseManager::CacheItem item;
                                    item.origUrl = image;
                                    item.finalUrl = image;
                                    item.type = "entry-image";
                                    emit addDownload(item);
                                }
                            } else {
                                // Checking if content contains image
                                QRegExp rx("<img\\s[^>]*src\\s*=\\s*(\"[^\"]*\"|'[^']*')", Qt::CaseInsensitive);
                                if (rx.indexIn(e.content)!=-1) {
                                    QString imgSrc = rx.cap(1); imgSrc = imgSrc.mid(1,imgSrc.length()-2);
                                    if (imgSrc!="") {
                                        if (!s->db->isCacheExistsByFinalUrl(Utils::hash(imgSrc))) {
                                            DatabaseManager::CacheItem item;
                                            item.origUrl = imgSrc;
                                            item.finalUrl = imgSrc;
                                            item.type = "entry-image";
                                            emit addDownload(item);
                                        }
                                        e.image = imgSrc;
                                        //qDebug() << "cap image:" << imgSrc;
                                    }
                                }
                            }
                        }

                        s->db->writeEntry(e);
                        ++entriesCount;

                        if (e.publishedAt>0)
                            publishedBeforeDate = e.publishedAt;

                        ++ai;
                    }
                } else {
                    qWarning() << "No \"items\" element found!";
                }
            }

            ++i;
        }
    }  else {
        qWarning() << "No \"relults\" element found!";
    }

    return entriesCount;
}

bool NvFetcher::checkError()
{
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
    if(jsonObj["error"].isObject()) {
        qWarning() << "Error in Netvibes response!";
        qWarning() << "Code:" << (int) jsonObj["error"].toObject()["code"].toDouble();
        qWarning() << "Message:" << jsonObj["error"].toObject()["message"].toString();
#else
    if (jsonObj["error"].type()==QVariant::Map) {
        qWarning() << "Error in Netvibes response!";
        qWarning() << "Code:" << (int) jsonObj["error"].toMap()["code"].toDouble();
        qWarning() << "Message:" << jsonObj["error"].toMap()["message"].toString();
#endif
        qWarning() << "JSON:" << jsonObj;
        return true;
    }

    return false;
}

void NvFetcher::setCookie(QNetworkRequest &request, const QString &cookie)
{
    QString value;
    //qDebug() << cookie;
    QStringList list = cookie.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    //qDebug() << cookie;
    QStringList::iterator i = list.begin();
    bool start = true;
    while (i != list.end()) {
        //qDebug() << "setCookie" << (*i);
        QStringList parts = (*i).split(';');
        //qDebug() << parts.at(0).split('=').at(1);
        if (parts.at(0).split('=').at(1) != "deleted") {
            if (!start)
                value = parts.at(0) + "; " + value;
            else
                value = parts.at(0);
            start = false;
        }
        ++i;
    }
    value = "lang=en_US; tz=2; "+value;
    //qDebug() << "setCookie value" << value;
    request.setRawHeader("Cookie",value.toLatin1());
}

bool NvFetcher::checkCookie(const QString &cookie)
{
    QStringList list = cookie.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    QStringList::iterator i = list.begin();
    while (i != list.end()) {
        //qDebug() << "checkCookie" << (*i);
        if ((*i).contains("activeSessionID",Qt::CaseSensitive)) {
            return true;
        }
        ++i;
    }

    return false;
}

void NvFetcher::uploadActions()
{
    if (!actionsList.isEmpty()) {
        emit uploading();
        //qDebug() << "Uploading actions...";
        setAction();
    }
}

void NvFetcher::cleanNewFeeds()
{
    Settings *s = Settings::instance();
    //QList<DatabaseManager::StreamModuleTab> storedStreamList = s->db->readStreamModuleTabListWithoutDate();
    QList<DatabaseManager::StreamModuleTab> tmp_storedStreamList = QList<DatabaseManager::StreamModuleTab>(storedStreamList);
    QList<DatabaseManager::StreamModuleTab>::iterator i = streamList.begin();
    //qDebug() << "###################################### tmp_storedStreamList.count" << tmp_storedStreamList.count();
    i = streamList.begin();
    while (i != streamList.end()) {
        //qDebug() << "streamList| streamId:" << (*i).streamId << "tabId:" << (*i).tabId;
        QList<DatabaseManager::StreamModuleTab>::iterator ci = tmp_storedStreamList.begin();
        bool newStream = true;
        while (ci != tmp_storedStreamList.end()) {
            //qDebug() << "tmp_storedStreamList| streamId:" << (*ci).streamId << "tabId:" << (*ci).tabId;
            if ((*i).streamId==(*ci).streamId && (*i).tabId==(*ci).tabId) {
                i = streamList.erase(i);
                tmp_storedStreamList.erase(ci);
                newStream = false;
                break;
            }

            if ((*i).streamId==(*ci).streamId) {
                qDebug() << "Old stream" << (*i).streamId << "in new tab" << (*i).tabId;
            }
            ++ci;
        }
        if (newStream) {
            qDebug() << "New stream" << (*i).streamId << "in tab" << (*i).tabId;
            ++i;
        }
    }
}

void NvFetcher::cleanRemovedFeeds()
{
    Settings *s = Settings::instance();
    QList<DatabaseManager::StreamModuleTab>::iterator i = storedStreamList.begin();

    while (i != storedStreamList.end()) {
        bool removedStream = true;
        QList<DatabaseManager::StreamModuleTab>::iterator ci = streamList.begin();
        while (ci != streamList.end()) {
            if ((*i).streamId == (*ci).streamId) {
                removedStream = false;
                if ((*i).tabId == (*ci).tabId) {
                    //qDebug() << "Existing stream" << (*i).streamId << "in tab" << (*i).tabId;
                } else {
                    qDebug() << "Old stream" << (*i).streamId << "in new tab" << (*ci).tabId;
                }
                break;
            }

            ++ci;
        }

        if (removedStream) {
            qDebug() << "Removing stream" << (*i).streamId << "in tab" << (*i).tabId;
            s->db->removeStreamsByStream((*i).streamId);

            // Removing stream from streamUpdateList
            QList<DatabaseManager::StreamModuleTab>::iterator sui = streamUpdateList.begin();
            while (sui != streamUpdateList.end()) {
                if ((*sui).streamId==(*i).streamId && (*sui).tabId==(*i).tabId) {
                    //qDebug() << "Removing stream form streamUpdateList" << (*sui).streamId;
                    streamUpdateList.erase(sui);
                    break;
                }
                ++sui;
            }
        }

        ++i;
    }
}

void NvFetcher::removeAction()
{
    DatabaseManager::Action action = actionsList.takeFirst();

    Settings *s = Settings::instance();
    s->db->removeActionsById(action.id1);

    if (actionsList.isEmpty()) {
        s->db->cleanDashboards();
        startFetching();
    } else {
        uploadActions();
    }
}