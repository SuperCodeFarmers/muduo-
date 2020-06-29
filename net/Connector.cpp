// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Connector.h"

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/SocketsOps.h"

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
	: loop_(loop),
	serverAddr_(serverAddr),
	connect_(false),
	state_(kDisconnected),
	retryDelayMs_(kInitRetryDelayMs)
{
	LOG_DEBUG << "ctor[" << this << "]";
}

Connector::~Connector()
{
	LOG_DEBUG << "dtor[" << this << "]";
	assert(!channel_);
}

// ���Կ��̵߳���
void Connector::start()
{
	connect_ = true;
	loop_->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
}

void Connector::startInLoop()
{
	// ���Դ���IO�߳���
	loop_->assertInLoopThread();
	assert(state_ == kDisconnected);
	if (connect_)
	{
		// ��������
		connect();
	}
	else
	{
		LOG_DEBUG << "do not connect";
	}
}

//�رպ��������Կ��̵߳���
void Connector::stop()
{
	// �ر�connector
	connect_ = false;

	// ��IO�߳��е���stopInLoop
	loop_->queueInLoop(std::bind(&Connector::stopInLoop, this)); // FIXME: unsafe
	// FIXME: cancel timer
}

void Connector::stopInLoop()
{
	loop_->assertInLoopThread();

	// ���״̬������������
	if (state_ == kConnecting)
	{
		setState(kDisconnected);

		// ��ͨ����poller���Ƴ���ע������channel�ÿ�
		int sockfd = removeAndResetChannel();
		retry(sockfd);	// ���ﲢ���Ƿ�Ҫ������ֻ�ǵ���sockets::close(sockfd)
	}
}

void Connector::connect()
{
	// ����һ���������׽���
	int sockfd = sockets::createNonblockingOrDie(serverAddr_.family());
	int ret = sockets::connect(sockfd, serverAddr_.getSockAddr());
	int savedErrno = (ret == 0) ? 0 : errno;
	switch (savedErrno)
	{
	case 0:
	case EINPROGRESS:	// �������׽��֣�δ���ӳɹ���������EINPROGRESS��ʾ��������
	case EINTR:
	case EISCONN:		// ���ӳɹ�
		connecting(sockfd);
		break;

	case EAGAIN:
	case EADDRINUSE:
	case EADDRNOTAVAIL:
	case ECONNREFUSED:
	case ENETUNREACH:
		retry(sockfd);	// ����
		break;

	case EACCES:
	case EPERM:
	case EAFNOSUPPORT:
	case EALREADY:
	case EBADF:
	case EFAULT:
	case ENOTSOCK:
		LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
		sockets::close(sockfd);	// �����������ر�sockfd
		break;

	default:
		LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
		sockets::close(sockfd);
		// connectErrorCallback_();
		break;
	}
}

// ���������ܿ��̵߳���
void Connector::restart()
{
	loop_->assertInLoopThread();
	setState(kDisconnected);

	// ��ʼ������ʱ��
	retryDelayMs_ = kInitRetryDelayMs;
	connect_ = true;
	startInLoop();
}

void Connector::connecting(int sockfd)
{
	setState(kConnecting);
	assert(!channel_);
	// ����һ��channel������sockfd��������
	// ������Connector�����캯��ʱ,��û��ֱ�Ӵ���channel����ֻ��sockfd�����ɹ��˲Ŵ���channel����
	channel_.reset(new Channel(loop_, sockfd));
	
	// ���ÿ�д�ص���������ʱ�����socketû�д���sockfd�ʹ��ڿ�д״̬
	// һ�����ӳɹ����ͻ����handleWrite
	channel_->setWriteCallback(
		std::bind(&Connector::handleWrite, this)); // FIXME: unsafe

	// ���ô���ص�����
	channel_->setErrorCallback(
		std::bind(&Connector::handleError, this)); // FIXME: unsafe

	// channel_->tie(shared_from_this()); is not working,
	// as channel_ is not managed by shared_ptr
	channel_->enableWriting();	// ��Poller��ע��д�¼������ӳɹ�֮������handleWrite
}

int Connector::removeAndResetChannel()
{
	channel_->disableAll();
	channel_->remove();		// ��poller���Ƴ���ע
	int sockfd = channel_->fd();
	// Can't reset channel_ here, because we are inside Channel::handleEvent
	// ��������������channel_����Ϊ�������ڵ���Channel::handleEvent
	// �������ǰ�resetChannel��������У�Ȼ�����Ǿ�����������������ִ��resetChannel
	loop_->queueInLoop(std::bind(&Connector::resetChannel, this)); // FIXME: unsafe
	return sockfd;
}

void Connector::resetChannel()
{
	// ��channel�ÿ�
	channel_.reset();
}

void Connector::handleWrite()
{
	LOG_TRACE << "Connector::handleWrite " << state_;

	if (state_ == kConnecting)
	{
		// ������ӳɹ���,���channel��û�м�ֵ��
		// Ϊʲô�ѿ�д״̬ȡ����ע����Ϊ�������׽��ֶ��ڵ�ƽ������һֱ���ڿ�д״̬������Ҫȡ����ע
		int sockfd = removeAndResetChannel();	// ��Poller���Ƴ���ע������channel�ÿ�

		// socket��д������ζ������һ�������ɹ�
		// ����Ҫ��getsocktopt(sockfd, SOL_SOL_SOCKET, SO_ERROR, ...)�ٴ�ȷ��һ��
		int err = sockets::getSocketError(sockfd);
		if (err)	// �д���
		{
			LOG_WARN << "Connector::handleWrite - SO_ERROR = "
				<< err << " " << strerror_tl(err);
			retry(sockfd);	// ����
		}
		else if (sockets::isSelfConnect(sockfd))	// ������
		{
			LOG_WARN << "Connector::handleWrite - Self connect";
			retry(sockfd);	// ����
		}
		else	// ���ӳɹ�
		{
			// ����״̬
			setState(kConnected);
			if (connect_)
			{
				newConnectionCallback_(sockfd);	// �ص�
			}
			else
			{
				sockets::close(sockfd);
			}
		}
	}
	else
	{
		// what happened?
		assert(state_ == kDisconnected);
	}
}

// ��������Ļص�����
void Connector::handleError()
{
	LOG_ERROR << "Connector::handleError state=" << state_;
	if (state_ == kConnecting)
	{
		// ��poller���Ƴ���ע������channel�ÿ�
		int sockfd = removeAndResetChannel();
		int err = sockets::getSocketError(sockfd);
		LOG_TRACE << "SO_ERROR = " << err << " " << strerror_tl(err);
		retry(sockfd);	// ���·�������
	}
}

// ����������back-off�����������������¼����ӳ���0.5s, 1s, 2s, ...ֱ��30s
void Connector::retry(int sockfd)
{
	// ����֮ǰ�ȹر��׽���
	sockets::close(sockfd);
	// ����״̬
	setState(kDisconnected);
	if (connect_)
	{
		LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort()
			<< " in " << retryDelayMs_ << " milliseconds. ";
		// ע��һ����ʱ����������һ����ʱ��
		loop_->runAfter(retryDelayMs_ / 1000.0,
			std::bind(&Connector::startInLoop, shared_from_this()));

		// �´�����ʱ��
		retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
	}
	else
	{
		LOG_DEBUG << "do not connect";
	}
}

