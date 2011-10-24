/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Hugo Lindström <hugolm84@gmail.com>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "spotifyPlugin.h"

#include <QDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QNetworkConfiguration>
#include <QNetworkReply>
#include <QDomElement>

#include "album.h"
#include "typedefs.h"
#include "audio/audioengine.h"
#include "tomahawksettings.h"
#include "utils/tomahawkutils.h"
#include "utils/logger.h"
#include "chartsplugin_data_p.h"

#define SPOTIFY_API_URL "http://spotikea.tomahawk-player.org:10380/"
#include <qjson/parser.h>
#include <qjson/serializer.h>

using namespace Tomahawk::InfoSystem;


SpotifyPlugin::SpotifyPlugin()
    : InfoPlugin()
{

    m_supportedGetTypes << InfoChart << InfoChartCapabilities;

}


SpotifyPlugin::~SpotifyPlugin()
{
    qDebug() << Q_FUNC_INFO;
}


void
SpotifyPlugin::namChangedSlot( QNetworkAccessManager *nam )
{
    tDebug() << "SpotifyPlugin: namChangedSLot";
    qDebug() << Q_FUNC_INFO;
    if( !nam )
        return;

    m_nam = QWeakPointer< QNetworkAccessManager >( nam );

    /// We need to fetch possible types before they are asked for
    tDebug() << "SpotifyPlugin: InfoChart fetching possible resources";

    QUrl url = QUrl( QString( SPOTIFY_API_URL "toplist/charts" )  );
    QNetworkReply* reply = m_nam.data()->get( QNetworkRequest( url ) );
    tDebug() << Q_FUNC_INFO << "fetching:" << url;
    connect( reply, SIGNAL( finished() ), SLOT( chartTypes() ) );

}


void
SpotifyPlugin::dataError( uint requestId, Tomahawk::InfoSystem::InfoRequestData requestData )
{
    emit info( requestId, requestData, QVariant() );
    return;
}


void
SpotifyPlugin::getInfo( uint requestId, Tomahawk::InfoSystem::InfoRequestData requestData )
{
    qDebug() << Q_FUNC_INFO << requestData.caller;
    qDebug() << Q_FUNC_INFO << requestData.customData;

    InfoStringHash hash = requestData.input.value< Tomahawk::InfoSystem::InfoStringHash >();


    switch ( requestData.type )
    {
        case InfoChart:
            if ( !hash.contains( "chart_source" ) || hash["chart_source"] != "spotify" )
            {
                dataError( requestId, requestData );
                break;
            }
            qDebug() << Q_FUNC_INFO << "InfoCHart req for" << hash["chart_source"];
            fetchChart( requestId, requestData );
            break;

        case InfoChartCapabilities:
            fetchChartCapabilities( requestId, requestData );
            break;
        default:
            dataError( requestId, requestData );
    }
}


void
SpotifyPlugin::pushInfo( const QString caller, const Tomahawk::InfoSystem::InfoType type, const QVariant input )
{
    Q_UNUSED( caller )
    Q_UNUSED( type)
    Q_UNUSED( input )
}

void
SpotifyPlugin::fetchChart( uint requestId, Tomahawk::InfoSystem::InfoRequestData requestData )
{
    if ( !requestData.input.canConvert< Tomahawk::InfoSystem::InfoStringHash >() )
    {
        dataError( requestId, requestData );
        return;
    }
    InfoStringHash hash = requestData.input.value< Tomahawk::InfoSystem::InfoStringHash >();
    Tomahawk::InfoSystem::InfoStringHash criteria;
    if ( !hash.contains( "chart_id" ) )
    {
        dataError( requestId, requestData );
        return;
    } else {
        criteria["chart_id"] = hash["chart_id"];
    }

    emit getCachedInfo( requestId, criteria, 0, requestData );
}
void
SpotifyPlugin::fetchChartCapabilities( uint requestId, Tomahawk::InfoSystem::InfoRequestData requestData )
{
    if ( !requestData.input.canConvert< Tomahawk::InfoSystem::InfoStringHash >() )
    {
        dataError( requestId, requestData );
        return;
    }

    Tomahawk::InfoSystem::InfoStringHash criteria;
    emit getCachedInfo( requestId, criteria, 0, requestData );
}

void
SpotifyPlugin::notInCacheSlot( uint requestId, QHash<QString, QString> criteria, Tomahawk::InfoSystem::InfoRequestData requestData )
{
    if ( !m_nam.data() )
    {
        tLog() << Q_FUNC_INFO << "Have a null QNAM, uh oh";
        emit info( requestId, requestData, QVariant() );
        return;
    }


    switch ( requestData.type )
    {

        case InfoChart:
        {
            /// Fetch the chart, we need source and id
            QUrl url = QUrl( QString( SPOTIFY_API_URL "toplist/%1/" ).arg( criteria["chart_id"] ) );
            qDebug() << Q_FUNC_INFO << "Getting chart url" << url;

            QNetworkReply* reply = m_nam.data()->get( QNetworkRequest( url ) );
            reply->setProperty( "requestId", requestId );
            reply->setProperty( "requestData", QVariant::fromValue< Tomahawk::InfoSystem::InfoRequestData >( requestData ) );
            connect( reply, SIGNAL( finished() ), SLOT( chartReturned() ) );
            return;


        }
        case InfoChartCapabilities:
        {
        qDebug() << Q_FUNC_INFO << "EMITTING CHART" << m_allChartsMap;
            emit info(
                requestId,
                requestData,
                m_allChartsMap
            );
            return;
        }

        default:
        {
            tLog() << Q_FUNC_INFO << "Couldn't figure out what to do with this type of request after cache miss";
            emit info( requestId, requestData, QVariant() );
            return;
        }
    }
}


void
SpotifyPlugin::chartTypes()
{
    /// Get possible chart type for specificSpotifyPlugin: InfoChart types returned chart source
    tDebug() << Q_FUNC_INFO << "Got spotifychart type result";
    QNetworkReply* reply = qobject_cast<QNetworkReply*>( sender() );

    if ( reply->error() == QNetworkReply::NoError )
    {
        QJson::Parser p;
        bool ok;
        const QVariantMap res = p.parse( reply, &ok ).toMap();
        const QVariantMap chartObj = res;

        if ( !ok )
        {
            tLog() << Q_FUNC_INFO << "Failed to parse resources" << p.errorString() << "On line" << p.errorLine();

            return;
        }

        QVariantMap charts;
        foreach(QVariant geos, chartObj.value("Charts").toList().takeLast().toMap().value("geo").toList() )
        {

           const QString geo = geos.toMap().value( "name" ).toString();
           const QString geoId = geos.toMap().value( "id" ).toString();
           QString country;

           if( geo == "For me" )
              continue; /// country = geo; Lets use this later, when we can get the spotify username from tomahawk
           else if( geo == "Everywhere" )
               country = geo;
           else
           {

               QLocale l( QString( "en_%1" ).arg( geo ) );
               country = Tomahawk::CountryUtils::fullCountryFromCode( geo );

               for ( int i = 1; i < country.size(); i++ )
               {
                   if ( country.at( i ).isUpper() )
                   {
                       country.insert( i, " " );
                       i++;
                   }
               }
           }

           QList< InfoStringHash > chart_types;
           foreach(QVariant types, chartObj.value("Charts").toList().takeFirst().toMap().value("types").toList() )
           {
               QString type = types.toMap().value( "id" ).toString();
               QString label = types.toMap().value( "name" ).toString();

               InfoStringHash c;
               c[ "id" ] = type + "/" + geoId;
               c[ "label" ] = label;
               c[ "type" ] = type;

               chart_types.append( c );

           }

           charts.insert( country.toUtf8(), QVariant::fromValue<QList< InfoStringHash > >( chart_types ) );

        }

        m_allChartsMap.insert( "Spotify", QVariant::fromValue<QVariantMap>( charts ) );

    }
    else
    {
        tLog() << Q_FUNC_INFO << "Error fetching charts:" << reply->errorString();
    }

}

void
SpotifyPlugin::chartReturned()
{

    /// Chart request returned something! Woho
    QNetworkReply* reply = qobject_cast<QNetworkReply*>( sender() );
    QString url = reply->url().toString();
    QVariantMap returnedData;
    if ( reply->error() == QNetworkReply::NoError )
    {
        QJson::Parser p;
        bool ok;
        QVariantMap res = p.parse( reply, &ok ).toMap();

        if ( !ok )
        {
            tLog() << "Failed to parse json from chart lookup:" << p.errorString() << "On line" << p.errorLine();
            return;
        }

        /// SO we have a result, parse it!
        QList< InfoStringHash > top_tracks;
        QList< InfoStringHash > top_albums;
        QStringList top_artists;

        if( url.contains( "albums" ) )
            setChartType( Album );
        else if( url.contains( "tracks" ) )
            setChartType( Track );
        else if( url.contains( "artists" ) )
            setChartType( Artist );
        else
            setChartType( None );

        foreach(QVariant result, res.value("toplist").toMap().value("result").toList() )
        {
            QString title, artist;
            QVariantMap chartMap = result.toMap();

            if ( !chartMap.isEmpty() )
            {

                title = chartMap.value( "title" ).toString();
                artist = chartMap.value( "artist" ).toString();

                if( chartType() == Track )
                {
                    InfoStringHash pair;
                    pair["artist"] = artist;
                    pair["track"] = title;
                    top_tracks << pair;

                    qDebug() << "SpotifyChart type is track";
                }

                if( chartType() == Album )
                {

                    InfoStringHash pair;
                    pair["artist"] = artist;
                    pair["album"] = title;
                    top_albums << pair;
                    qDebug() << "SpotifyChart type is album";
                }

                if( chartType() == Artist )
                {

                    top_artists << chartMap.value( "name" ).toString();
                    qDebug() << "SpotifyChart type is artist";

                }
            }
        }

        if( chartType() == Track )
        {
            tDebug() << "ChartsPlugin:" << "\tgot " << top_tracks.size() << " tracks";
            returnedData["tracks"] = QVariant::fromValue( top_tracks );
            returnedData["type"] = "tracks";
        }

        if( chartType() == Album )
        {
            tDebug() << "ChartsPlugin:" << "\tgot " << top_albums.size() << " albums";
            returnedData["albums"] = QVariant::fromValue( top_albums );
            returnedData["type"] = "albums";
        }

        if( chartType() == Artist )
        {
            tDebug() << "ChartsPlugin:" << "\tgot " << top_artists.size() << " artists";
            returnedData["artists"] = top_artists;
            returnedData["type"] = "artists";
        }

        Tomahawk::InfoSystem::InfoRequestData requestData = reply->property( "requestData" ).value< Tomahawk::InfoSystem::InfoRequestData >();


        emit info(
            reply->property( "requestId" ).toUInt(),
            requestData,
            returnedData
        );

    }
    else
        qDebug() << "Network error in fetching chart:" << reply->url().toString();

}
