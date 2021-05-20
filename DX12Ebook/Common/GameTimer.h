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

	float TotalTime()const; // in seconds
	/* 拿两帧之间的时间差*/
	float DeltaTime()const; // in seconds

	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop();  // Call when paused.

	/* 计算时刻差(单位:"计数")的函数*/
	void Tick();  // Call every frame.

private:
	double mSecondsPerCount;// 转换因子(实际上就是几分之一秒)
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;

	bool mStopped;
};

#endif // GAMETIMER_H