#ifndef _SYNCQUEUE_H_
#define _SYNCQUEUE_H_

#include <list>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

template<typename T>
class SyncQueue
{
public:
	/************************************************************************
	功  能：构造函数
	参  数：
		bWait：输入，是否以阻塞方式访问队列
			true：以阻塞方式访问队列
			false：以非阻塞方式访问队列，这是默认值
	返回值：无
	************************************************************************/
	SyncQueue(bool bWait = false)
	{
		m_maxSize = 0;
		m_bWait = bWait;
		m_bStopped = false;
	}

	/************************************************************************
	功  能：构造函数
	参  数：
		maxSize：输入，队列的最大长度
		bWait：输入，是否以阻塞方式访问队列
			true：以阻塞方式访问队列
			false：以非阻塞方式访问队列，这是默认值
	返回值：无
	************************************************************************/
	SyncQueue(unsigned int maxSize, bool bWait = false)
	{
		m_maxSize = maxSize;
		m_bWait = bWait;
		m_bStopped = false;
	}

	/************************************************************************
	功  能：向队列放一个左值
	参  数：
		x：输入，待放入队列的左值
	返回值：
		true：放成功
		false：放失败
	************************************************************************/
	bool put(T &x)
	{
		return add(x, true);
	}

	/************************************************************************
	功  能：向队列放一个右值
	参  数：
		x：输入，待放入队列的右值
	返回值：
		true：放成功
		false：放失败
	************************************************************************/
	bool put(T &&x)
	{
		return add(x, false);
	}

	/************************************************************************
	功  能：从队列取值
	参  数：
		x：输出，待从队列取出的值
	返回值：
		true：取成功
		false：取失败
	************************************************************************/
	bool take(T &x)
	{
		//若队列访问方式为非阻塞，且队列为空，则立即返回
		if (!bWait && m_queue.empty())
			return false;

		//以下操作加锁进行
		boost::unique_lock<boost::mutex> lock(m_mutex);
		if (bWait)
		{
			//当外部未调用过stop()时，同步等待队列的非空条件
			while (!m_bStopped && m_queue.empty())
				m_notEmpty.wait(lock);
			//方法开始时判断过，但在多线程环境下，现在必须重新判断
			if (m_queue.empty())
				return false;
		}
		else
		{
			//方法开始时判断过，但在多线程环境下，现在必须重新判断
			if (m_queue.empty())
				return false;
		}

		//从队列取值
		x = m_queue.front();
		m_queue.pop_front();
		if (m_bWait)
			//队列此时非满，唤醒等待队列的非满条件的线程
			m_notFull.notify_one();

		return true;
	}

	/************************************************************************
	功  能：从队列取所有值
	参  数：
		x：输出，待从队列取出的所有值
	返回值：
		true：取成功
		false：取失败
	************************************************************************/
	bool takeAll(std::list<T> &queue)
	{
		if (!m_bWait && m_queue.empty())
			return false;

		boost::unique_lock<boost::mutex> lock(m_mutex);
		if (m_bWait)
		{
			while (!m_bStopped && m_queue.empty())
				m_notEmpty.wait(lock);
			if (m_queue.empty())
				return false;
		}
		else
		{
			if (m_queue.empty())
				return false;
		}

		queue = m_queue;
		m_queue.clear();
		if (m_bWait)
			m_notFull.notify_one();

		return true;
	}

	/************************************************************************
	功  能：当队列访问方式为阻塞时，唤醒所有等待着的线程，此后队列只能以非阻塞的方式被访问
	参  数：无
	返回值：无
	************************************************************************/
	void stop()
	{
		if (m_bWait)
		{
			m_bStopped = true;
			//唤醒所有等待队列的非满条件的线程
			m_notFull.notify_all();
			//唤醒所有等待队列的非空条件的线程
			m_notEmpty.notify_all();
		}
	}

	/************************************************************************
	功  能：设置队列最大长度
	参  数：
		maxSize：输入，队列最大长度
	返回值：无
	************************************************************************/
	void setMaxSize(unsigned int maxSize)
	{
		//以下操作加锁进行
		boost::unique_lock<boost::mutex> lock(m_mutex);
		m_maxSize = maxSize;
	}

	/************************************************************************
	功  能：设置队列阻塞或非阻塞访问方式
	参  数：
		bWait：输入，是否以阻塞方式访问队列
			true：以阻塞方式访问队列
			false：以非阻塞方式访问队列
	返回值：无
	************************************************************************/
	void setWait(bool bWait)
	{
		//以下操作加锁进行
		boost::unique_lock<boost::mutex> lock(m_mutex);
		m_bWait = bWait;
	}

	/************************************************************************
	功  能：获取队列当前长度
	参  数：无
	返回值：队列当前长度
	************************************************************************/
	unsigned int getSize()
	{
		return m_queue.size();
	}

private:
	//向队列放一个，放成功返回true，放失败返回false
	/************************************************************************
	功  能：向队列放一个左值或右值
	参  数：
		x：输入，待放入队列的右值
		bLeftValue：输入，是左值还是右值
			true：是左值
			false：是右值
	返回值：
		true：放成功
		false：放失败
	************************************************************************/
	bool add(T &x, bool bLeftValue)
	{
		//若队列访问方式为非阻塞，且队列为满，则立即返回
		if (!m_bWait && m_maxSize <= m_queue.size())
			return false;

		//以下操作加锁进行
		boost::unique_lock<boost::mutex> lock(m_mutex);
		if (m_bWait)
		{
			//当外部未调用过stop()时，同步等待队列的非满条件
			while (!m_bStopped && m_maxSize <= m_queue.size())
				m_notFull.wait(lock);
			//方法开始时判断过，但在多线程环境下，现在必须重新判断
			if (m_maxSize <= m_queue.size())
				return false;
		}
		else
		{
			//方法开始时判断过，但在多线程环境下，现在必须重新判断
			if (m_maxSize <= m_queue.size())
				return false;
		}

		//向队列放一个左值或右值
		if (bLeftValue)
			m_queue.push_back(x);
		else
			m_queue.push_back(move(x));
		if (m_bWait)
			//队列此时非空，唤醒等待队列的非空条件的线程
			m_notEmpty.notify_one();

		return true;
	}

	//队列
	std::list<T> m_queue;
	//队列最大长度
	unsigned int m_maxSize;
	//是否以阻塞方式访问队列：当从空队列取数据，或向满队列放数据时，是否阻塞调用者线程直到队列非空，或队列非满
	bool m_bWait;
	//见stop()的描述
	bool m_bStopped;
	//互斥量，用于队列非空、队列非满条件变量，及本类其它必要的写临界区的操作
	boost::mutex m_mutex;
	//队列非空条件变量
	boost::condition_variable m_notEmpty;
	//队列非满条件变量
	boost::condition_variable m_notFull;
};

#endif