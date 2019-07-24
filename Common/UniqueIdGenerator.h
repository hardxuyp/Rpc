#ifndef _UNIQUEIDGENERATOR_H_
#define _UNIQUEIDGENERATOR_H_

#include <set>

//唯一id生成器
//类型T必须是无符号整型
template<typename T>
class UniqueIdGenerator
{
public:
	/************************************************************************
	功  能：构造函数
	参  数：
		maxId：输入，id的最大值，id的类型必须是无符号整型，默认值为相应类型取值范围内的最大值
	返回值：无
	************************************************************************/
	UniqueIdGenerator(T maxId = ~((T)0))
	{
		m_maxId = maxId;
		m_bAllDirty = false;
		m_curCleanUsableId = 0;
	}

	/************************************************************************
	功  能：生成一个新的id
	参  数：
		id：输出，生成的新的id
	返回值：
		true：生成成功
		false：由于无可用id导致生成失败
	************************************************************************/
	bool generate(T &id)
	{
		bool bRet = true;
		if (!m_bAllDirty)
		{
			id = m_curCleanUsableId;
			if (m_curCleanUsableId >= m_maxId)
				m_bAllDirty = true;
			else
				++m_curCleanUsableId;
		}

		else
		{
			if (m_setDirtyId.empty())
				bRet = false;
			else
			{
				auto it = m_setDirtyId.begin();
				id = *it;
				m_setDirtyId.erase(it);
			}
		}
		return bRet;
	}

	/************************************************************************
	功  能：归还id给id资源池
	参  数：
		id：输入，待归还的id
	返回值：无
	************************************************************************/
	void back(T &id)
	{
		m_setDirtyId.insert(id);
	}

private:
	//id的最大值
	T m_maxId;
	//是否所有id都是脏的，脏：id被生成过，干净：id未被生成过
	bool m_bAllDirty;
	//当前干净的可用id
	T m_curCleanUsableId;
	//脏id集合，被归还的id放于此集合
	std::set<T> m_setDirtyId;
};

#endif