#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;	// 当前已使用的连接数
	m_FreeConn = 0; // 当前空闲的连接数
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool; // 单例模式创建数据库实例 只有第一次调用该函数时 才会创建变量
	return &connPool;
}

// 构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;			 // 主机地址 (数据库地址)
	m_Port = Port;			 // 数据库端口号
	m_User = User;			 // 登陆数据库用户名
	m_PassWord = PassWord;	 // 登陆数据库密码
	m_DatabaseName = DBName; // 使用数据库名
	m_close_log = close_log; // 日志开关

	for (int i = 0; i < MaxConn; i++)
	{
		/*
			MYSQL *是MySQL C API中定义的指向MYSQL结构体的指针类型。
			它用于在C/C++程序中表示与MySQL数据库的连接。通过使用MYSQL *类型的指针变量，
			可以进行数据库连接的创建、查询、更新等操作。
		*/
		MYSQL *con = nullptr;
		con = mysql_init(con); // 它使用mysql_init()函数来分配并初始化一个MYSQL结构体对象

		if (con == nullptr) // 初始化失败
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		/*
			它使用mysql_real_connect函数来建立与MySQL数据库的连接。具体参数如下：
			- con：一个指向MYSQL结构体的指针，用于表示与数据库的连接。
			- url.c_str()：数据库的主机地址。
			- User.c_str()：登录数据库的用户名。
			- PassWord.c_str()：登录数据库的密码。
			- DBName.c_str()：要使用的数据库名。
			- Port：数据库的端口号。
			- nullptr：用于指定UNIX套接字文件路径，如果使用TCP/IP连接，则为nullptr。
			- 0：用于指定客户端标志。
			如果连接成功，con将指向一个有效的MYSQL对象，表示与数据库的连接。如果连接失败，con将为nullptr。
		*/
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, nullptr, 0);

		if (con == nullptr)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con); // 放入连接池中
		++m_FreeConn;			 // 空闲连接数+1
	}
	// reserve = sem(m_FreeConn); // 创建了一个名为reserve的信号量对象，并将其初始化为m_FreeConn的值
	m_MaxConn = m_FreeConn; // m_FreeConn的值赋给了m_MaxConn
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = nullptr;

	if (0 == connList.size())
		return nullptr;

	// reserve.wait(); // 获取信号量 没有就阻塞等待
	// lock.lock(); // 上锁

	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this]
			 { return !this->connList.empty(); });
	con = connList.front();
	connList.pop_front();

	--m_FreeConn; // 空闲连接数-1
	++m_CurConn;  // 正在使用连接数+1

	// lock.unlock(); // 解锁
	return con; // 返回数据库连接对象
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (nullptr == con)
		return false;

	// lock.lock(); // 上锁
	std::lock_guard<std::mutex> lock(mutex_);
	connList.push_back(con); // 放入连接池链表中
	++m_FreeConn;			 // 空弦数+1
	--m_CurConn;			 // 正在使用数-1
	// lock.unlock(); // 解锁
	// reserve.post(); // 释放信号量
	cv_.notify_one();
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{

	// lock.lock();
	std::lock_guard<std::mutex> lock(mutex_);
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}
	// lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection(); // 取出池中的一个连接
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII() // 将连接 放回连接池的链表中
{
	poolRAII->ReleaseConnection(conRAII);
}