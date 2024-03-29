

#include "clientprotocol.h"
#include <QTcpSocket>
#include <QDateTime>

NetworkClient::NetworkClient (const QString &dstHost, const quint16 dstPort , QObject* parent) :
    QObject(parent),
    dstHost( dstHost ),
    dstPort( dstPort ),
    protocolRetry( 0 ),
    maxProtocolRetries( 10 ),
    protocolStarted( false )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "Protocol(...)";
    #endif
    connectTimeoutTimer = new QTimer( this );
    connectTimeoutTimer->setInterval(connectTimeout);
    connectTimeoutTimer->setSingleShot( true );
    connect( connectTimeoutTimer, SIGNAL(timeout()), SLOT(onConnectTimeout()) );
    reconnectSleepTimer = new QTimer( this );
    reconnectSleepTimer->setSingleShot( true );
    reconnectSleepTimer->setInterval(reconnectSleep);
    connect( reconnectSleepTimer, SIGNAL(timeout()), SLOT(deferredStart()) );
}

NetworkClient::~NetworkClient()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "~Protocol()";
    #endif
}

QAbstractSocket *NetworkClient::buildSocket()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "buildSocket()";
    #endif
    return new QTcpSocket;
}

void NetworkClient::deferredStart()
{
    #ifdef DEBUG_PROTOCOL
    qDebug()<<QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << this << "deferredStart()";
    #endif
    //if ( protocolRetry == 0 )
    if( socket.isNull() )
        {
            socket.reset(buildSocket());
            connect( socket.data(), SIGNAL( error( QAbstractSocket::SocketError)),
                     SLOT( onSocketError( QAbstractSocket::SocketError)) );
            connect( socket.data(), SIGNAL( stateChanged( QAbstractSocket::SocketState)),
                     SLOT( onSocketStateChanged( QAbstractSocket::SocketState)) );
        }

    if ( protocolRetry >= maxProtocolRetries )
        {
            emitError( "Maximum protocol retries has reached" );
            return;
        }

    if ( !connectTimeoutTimer->isActive() )
        connectTimeoutTimer->start( connectTimeout );
    if ( reconnectSleepTimer->isActive() )
        reconnectSleepTimer->stop();

    switch ( socket->state() )
        {
        case QAbstractSocket::UnconnectedState: connectSocket(); break;
        case QAbstractSocket::ConnectedState:   protocolStart(); break;
        default:
                #ifdef DEBUG_PROTOCOL
                qDebug() << this << "Unexpected socet state."<<socket->state();
                #endif
            break;
        }
}

void NetworkClient::connectSocket()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "connectSocket()";
    #endif
    socket->connectToHost( dstHost, dstPort );
}

void NetworkClient::onConnectTimeout()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "onConnectTimeout()";
    #endif
    emitError( "Connect timeout" );
}

void NetworkClient::onSocketStateChanged( QAbstractSocket::SocketState socketState )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "onSocketStateChanged(" << socketState << ")";
    #endif
    if ( protocolRetry >= maxProtocolRetries )
        {
            emitError( "maxProtocolRetries reached" );
            return;
        }

    switch ( socketState )
        {
        case QAbstractSocket::ConnectedState:
            if ( !protocolStarted )
                protocolStart();
            break;
        case QAbstractSocket::UnconnectedState:
            if ( protocolStarted )
                {
                    protocolStop();
                }
            else
                {
                    //reconnectSleepTimer->start( reconnectSleep );
                }
            break;
        default:
            break;
        }
}

void NetworkClient::onSocketError( QAbstractSocket::SocketError socketError )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "onSocketError(" << socketError << ")";
    #endif
    switch ( socketError )
        {
        case QAbstractSocket::ConnectionRefusedError:
        case QAbstractSocket::SocketTimeoutError:
        case QAbstractSocket::RemoteHostClosedError:
            break;
        case QAbstractSocket::HostNotFoundError:
            qWarning() << this << "onSocketError(): host not found" << dstHost << ":" << dstPort;
            emitError( QString( "Host not found: ") + dstHost + ":" + QString::number( dstPort) );
            break;
        case QAbstractSocket::SocketAccessError:
        case QAbstractSocket::SocketResourceError:
        case QAbstractSocket::DatagramTooLargeError:
        case QAbstractSocket::AddressInUseError:
        case QAbstractSocket::NetworkError:
        case QAbstractSocket::SocketAddressNotAvailableError:
        case QAbstractSocket::UnsupportedSocketOperationError:
        case QAbstractSocket::ProxyAuthenticationRequiredError:
        case QAbstractSocket::UnknownSocketError:
            qCritical() << this << "onSocketError(): bad socket error, aborting" << socketError;
            emitError( "Bad socket error" );
            break;
        default:
            qCritical() << this << "onSocketError(): unknown socket error" << socketError;
            emitError( "Unknown socket error" );
        }
}

void NetworkClient::protocolStart()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "protocolStart()";
    #endif
    stopTimers();
    protocolRetry++;
    protocolStarted = true;
    connect( socket.data(), SIGNAL( readyRead()), this, SLOT( onReadyRead()) );
    connect( socket.data(), SIGNAL( bytesWritten( qint64)), this, SLOT( onBytesWritten( qint64)) );
}

void NetworkClient::protocolStop()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "protocolStop()";
    #endif
    protocolStarted = false;
    protocolRetry--;
    disconnect( socket.data(), SIGNAL( readyRead()), this, SLOT( onReadyRead()) );
    disconnect( socket.data(), SIGNAL( bytesWritten( qint64)), this, SLOT( onBytesWritten( qint64)) );
    socket->abort();
}

void NetworkClient::onBytesWritten( qint64  bytes  )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "onBytesWritten(" << bytes << ")";
    #else
    Q_UNUSED(bytes)
    #endif
}

void NetworkClient::onReadyRead()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "onReadyRead()";
    #endif

    QByteArray  data= socket->readAll();
    #ifdef DEBUG_PROTOCOL
    qDebug() << data;
    #endif
}

void NetworkClient::emitError( const QString &errTxt )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "emitError(...)";
    #endif
    if ( protocolStarted )
        protocolStop();
    else
        stopTimers();

    socket->abort();

    emit    error( errTxt );
}

void NetworkClient::emitDone()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "emitDone()";
    #endif
    if ( protocolStarted )
        protocolStop();
    else
        stopTimers();

    emit    done();
}

void NetworkClient::sureWrite( const QByteArray &response )
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "sureWrite(...)" << endl << response;
    #endif

    qint64      len  = response.size();
    const char  *ptr = response.data();
    while ( len )
        {
            qint64  res= socket->write( ptr, len );
            if ( res < 0 )
                {
                    #ifdef DEBUG_PROTOCOL
                    qCritical() << this << socket->errorString();
                    #endif
                    break;
                }
            len-= res;
            ptr+= res;
        }

    //socket->flush();
}

void NetworkClient::stopTimers()
{
    #ifdef DEBUG_PROTOCOL
    qDebug() << this << "stopTimers()";
    #endif
    if ( connectTimeoutTimer->isActive() )
        connectTimeoutTimer->stop();
    if ( reconnectSleepTimer->isActive() )
        reconnectSleepTimer->stop();
}

