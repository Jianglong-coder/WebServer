#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
// #include "../lock/locker.h"
#include <mutex>
#include <condition_variable>
#include "../log/log.h"

using namespace std;

// 数据库连接池类
class connection_pool
{
public:
	MYSQL *GetConnection();				 // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn); // 释放连接
	int GetFreeConn();					 // 获取空闲连接数量
	void DestroyPool();					 // 销毁所有连接

	// 单例模式
	static connection_pool *GetInstance(); // 返回一个数据库连接池的实例

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	connection_pool(); // 单例模式下 构造函数为private
	~connection_pool();

	int m_MaxConn;	// 最大连接数
	int m_CurConn;	// 当前已使用的连接数
	int m_FreeConn; // 当前空闲的连接数
	// locker lock;			// 锁
	std::mutex mutex_;		// 锁
	list<MYSQL *> connList; // 连接池
	// sem reserve;			// 信号量
	std::condition_variable cv_; // 信号量
	/*
		MYSQL *是MySQL C API中定义的指向MYSQL结构体的指针类型。
		它用于在C/C++程序中表示与MySQL数据库的连接。通过使用MYSQL *类型的指针变量，
		可以进行数据库连接的创建、查询、更新等操作。
	*/
public:
	string m_url;		   // 主机地址 (数据库地址)
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

// 利用RAII机制(主要应用在智能指针) 管理资源 避免内存泄露的方法
class connectionRAII
{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;			   // 数据库连接类型
	connection_pool *poolRAII; // 连接池类型
};

#endif
