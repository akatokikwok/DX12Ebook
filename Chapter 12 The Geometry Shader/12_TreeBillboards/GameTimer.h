//***************************************************************************************
// GameTimer.h by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#ifndef GAMETIMER_H
#define GAMETIMER_H

class GameTimer
{
public:
	/* 计时器类的构造函数会查询 计数器的频率, 对其取倒数之后就拿到转换因子(实际上就是几分之一秒)*/
	GameTimer();
	
	/* 计算到现在为止不算暂停的游玩时长*/
	float TotalTime()const; // in seconds
	
	/* 拿两帧之间的时间差*/
	float DeltaTime()const; // in seconds
	
	/* 重置base时刻以及前一时刻为当前时刻*/
	void Reset(); // Call before message loop.
	/* 此方法主要是计算累加的暂停时长 同时更新前一时刻为startTime时刻*/
	void Start(); // Call when unpaused.
	/* 存储被停止的时刻,同时命中停止标志*/
	void Stop();  // Call when paused.

	/* 计算时刻差(单位:"计数")的函数*/
	void Tick();  // Call every frame.

private:
	double mSecondsPerCount;// 转换因子(实际上就是几分之一秒)
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;// 应该理解为停止的时长,而非时刻
	__int64 mStopTime;// 被停止的时刻
	__int64 mPrevTime;
	__int64 mCurrTime;

	bool mStopped;
};

#endif // GAMETIMER_H