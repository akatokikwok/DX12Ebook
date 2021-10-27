// Fill out your copyright notice in the Description page of Project Settings.


#include "GFH.h"
#include "Weapons/GFHWeapon_Gun.h"
#include "UMG/Public/Blueprint/WidgetLayoutLibrary.h"

#define AdditionalSpreadFactor 1.0f;

static int32 GAllowRecoilDebug = 0;
static FAutoConsoleVariableRef CVarAllowRecoilDebug(
	TEXT("weapon.AllowRecoilDebug"),
	GAllowRecoilDebug,
	TEXT("If true, use enable recoil and spread debug and show debug references.")
);

AGFHWeapon_Gun::AGFHWeapon_Gun()
{

	// Set this actor to never tick
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
	NetUpdateFrequency = GFH_NET_UPDATE_FREQUNCY; // Set this to a value that's appropriate for your game

	//---------RecoilParam---------

	mRecoil_Angle_Pitch = 0.0f;
	mRecoil_Speed_Pitch = 0.0f;
	mRecoil_Angle_Yaw = 0.0f;
	mRecoil_Speed_Yaw = 0.0f;
	mRecoilCounter = 0.0f;
	mCurSpread = 0.0f;

	mIsShooting = false;

	mShootingTimer = 0.0f;

	mCameraShotPos = FVector::ZeroVector;
	mCameraShotDir = FVector::ZeroVector;

	mRecoilSpeedPitchDir = 1.0f;
	mRecoilSpeedYawDir = 1.0f;

}

void AGFHWeapon_Gun::BeginPlay()
{

	Super::BeginPlay();

	mShootingInterval = mShootingSpeed / 60.0f;

	mSpreadDecaySpeed = mStandSpreadDecaySpeed;

	mSpreadMinFactor = mStandSpreadMinFactor;

	mCurSpread = mStandSpreadInitial;

}

void AGFHWeapon_Gun::OnFireEnd()
{

	mShootingTimer = 0.0f;

	mIsShooting = true;

	mRecoilCounter = mRecoilCounter + 1.0f;

	if (mRecoilCounter > mRecoilCountMax)
	{
		mRecoilCounter = mRecoilCountMax;
	}

	// update recoil and spread for next step use
	GenNewRecoil();
	GenNewSpread();

	//mRecoil_Angle_Yaw = 0.0f;
	//mRecoil_Angle_Pitch = 0.0f;

	if (mRecoilPitchMin  > 0.0f || mRecoilYawMin > 0.0f)
	{
		mRecoil_Angle_Pitch += mRecoilPitchMin;
		mRecoil_Angle_Yaw += mRecoilYawMin;
		ApplyRecoilToCamera(FRotator(mRecoilPitchMin, mRecoilYawMin, 0.0f));
	}

}

void AGFHWeapon_Gun::Tick(float DeltaTime)
{

	Super::Tick(DeltaTime);
	
	if (GetLocalRole() < ROLE_Authority)
	{
		
		TickRecoil(DeltaTime);

		TickSpread(DeltaTime);

		if (GAllowRecoilDebug)
		{
			float CurCharSpeed = GetCharSpeed();// gunner's speed

			float MoveSpread = CurCharSpeed * mSpreadMovementFactor;// MoveSpeed = gunner speed * movespreadfactor

			float PosMoveSpread = mCurSpread + MoveSpread;
			// Clamp finalSpread Value <= max
			float FinalSpread = mCurSpread + MoveSpread;
			FinalSpread = FMath::Min(PosMoveSpread, mMaxSpreadMovement);

			//UE_LOG(LogTemp, Warning, 
			//	TEXT("%RecoilCounter %02f,FinalSpread %04f,PosSpread %04f,MoveSpread %04f"), 
			//	mRecoilCounter, FinalSpread, PosMoveSpread, MoveSpread);
		}

	}
	
}

void AGFHWeapon_Gun::TickRecoil(float DeltaTime)
{

	FVector2D SpeedVector(mRecoil_Speed_Pitch, mRecoil_Speed_Yaw);
	
	//----------angle-increase--------
	
	//if(mRecoil_Speed_Pitch > 0.0f)
	if (FMath::Abs(SpeedVector.Size()) > 0.0f)
	{
		TickRecoilAngleUp(DeltaTime);
	}
	else if(mIsShooting == false)
	//----------angle-decrease--------
	{
		TickRecoilAngleDown(DeltaTime);

		//UE_LOG(LogTemp, Warning, TEXT("--------------end--------------"));

		//UE_LOG(LogTemp, Warning, TEXT("CurRecoilCounter:%f"), mRecoilCounter);

		//UE_LOG(LogTemp, Warning, TEXT("mRecoil_Speed_Yaw:%f"), mRecoil_Speed_Yaw);

		//UE_LOG(LogTemp, Warning, TEXT("YawAngleDelta:%f"), YawAngleDelta);

		//UE_LOG(LogTemp, Warning, TEXT("AnglePitch:%f,AngleYaw:%f"), mRecoil_Angle_Pitch, mRecoil_Angle_Yaw);
	}

	TickRecoilCounterDown(DeltaTime);

}

void AGFHWeapon_Gun::TickRecoilAngleUp(float DeltaTime)
{
	
	float PitchAngleDelta = IncreaseRecoilAngle(mRecoil_Speed_Pitch, mRecoil_Angle_Pitch, mRecoilSpeedPitchDir, mRecoilPitchMax, DeltaTime);

	float YawAngleDelta =  IncreaseRecoilAngle(mRecoil_Speed_Yaw, mRecoil_Angle_Yaw, mRecoilSpeedYawDir, mRecoilYawMax, DeltaTime);

	if (FMath::IsNearlyZero(PitchAngleDelta) == false || FMath::IsNearlyZero(YawAngleDelta) == false)
	{

		//if (mRecoilCounter >= 8)
		{

			//UE_LOG(LogTemp, Warning, TEXT("--------------begin--------------"));

			//UE_LOG(LogTemp, Warning, TEXT("CurRecoilCounter:%f"), mRecoilCounter);

			//UE_LOG(LogTemp, Warning, TEXT("mRecoil_Speed_Yaw:%f"), mRecoil_Speed_Yaw);

			//UE_LOG(LogTemp, Warning, TEXT("YawAngleDelta:%f"), YawAngleDelta);

			//UE_LOG(LogTemp, Warning, TEXT("AnglePitch:%f,AngleYaw:%f"), mRecoil_Angle_Pitch, mRecoil_Angle_Yaw);


		}
		
		ApplyRecoilToCamera(FRotator(mRecoilSpeedPitchDir * PitchAngleDelta, mRecoilSpeedYawDir * YawAngleDelta, 0.0f));
		UE_LOG(LogTemp, Warning, TEXT("When UPUPUP offset Angle in Yaw Axis::: %f"), PitchAngleDelta);
	}
	
}

float AGFHWeapon_Gun::IncreaseRecoilAngle(float& CurSpeed, float& CurAngle, float SpeedDir, float MaxRecoil, float DeltaTime)
{

	//-----------angle-accumulation-----

	float AngleDelta = DeltaTime * CurSpeed;

	if (CurAngle < MaxRecoil)
	{

		float BeginAngle = CurAngle;

		CurAngle += SpeedDir * AngleDelta;

		if (FMath::Abs(CurAngle) > MaxRecoil)
		{
			//AngleDelta = CurAngle - MaxRecoil;
			AngleDelta = MaxRecoil - FMath::Abs(BeginAngle);
			CurAngle = SpeedDir * MaxRecoil;
		}

/*
		UE_LOG(LogTemp, Warning, TEXT("---------AngleDelta-------:%f"), AngleDelta);
		UE_LOG(LogTemp, Warning, TEXT("---------CurAngle-------:%f"), CurAngle);*/

		CurSpeed += DeltaTime * mRecoilReturnAcc;

		if (CurSpeed < 0.0f)
		{
			CurSpeed = 0.0f;
		}

	}
	else
	{

		AngleDelta = 0.0f;
		CurSpeed = 0.0f;

	}

	return AngleDelta;

}

void AGFHWeapon_Gun::TickRecoilAngleDown(float DeltaTime)
{

	float PitchDecayDelta = DecreaseRecoilAngle(mRecoil_Speed_Pitch, mRecoil_Angle_Pitch, mRecoilSpeedPitchDir, 0, DeltaTime);

	float YawDecayDelta = DecreaseRecoilAngle(mRecoil_Speed_Yaw, mRecoil_Angle_Yaw, mRecoilSpeedYawDir, 0, DeltaTime);

	// rotate camera
	// - mRecoilSpeedPitchDir *  - mRecoilSpeedYawDir *
	if (FMath::IsNearlyZero(PitchDecayDelta) == false || FMath::IsNearlyZero(YawDecayDelta) == false)
	{
		// make obvious
		float YawDecayDelta_cal = YawDecayDelta;
		if (FMath::IsNearlyZero(YawDecayDelta)) {
			YawDecayDelta_cal *= 1;
		}

		ApplyRecoilToCamera(FRotator(PitchDecayDelta, YawDecayDelta_cal, 0.0f));
		UE_LOG(LogTemp, Warning, TEXT("When DOWNDOWN offset Angle in Yaw Axis::: %f"), PitchDecayDelta);

	}


}

float AGFHWeapon_Gun::DecreaseRecoilAngle(float& RecoilSpeed, float& RecoilAngle, float SpeedDir, float Threshold, float DeltaTime)
{
	float PitchDecayDelta = 0.0f;

	if (FMath::IsNearlyZero(RecoilAngle) == false)
	{
		float initV = FMath::Abs(RecoilSpeed);
		float accV_value = FMath::Abs(mRecoilReturnAcc * mRecoilReturnAccFactor);

		RecoilSpeed += DeltaTime * mRecoilReturnAcc * mRecoilReturnAccFactor;// Vt = a * dt
		PitchDecayDelta = FMath::Abs(RecoilSpeed * DeltaTime);		 // a * dt *dt
		//PitchDecayDelta = FMath::Abs(1/2 * accV_value * DeltaTime * DeltaTime + );
		PitchDecayDelta = FMath::Min(FMath::Abs(RecoilAngle), PitchDecayDelta);

		float BeginRecoil = RecoilAngle;
		float RecoilAngleSign = FMath::Sign(BeginRecoil);
		PitchDecayDelta = RecoilAngleSign * PitchDecayDelta;// 偏移角和上一次开枪的角度保持同向

		RecoilAngle -= PitchDecayDelta;

		if (BeginRecoil * RecoilAngle <= 0.0f)
		{
			RecoilAngle = 0.0f;
			RecoilSpeed = 0.0f;
		}

		//UE_LOG(LogTemp, Warning, TEXT("+++++++++++RecoilAngle+++++++++++:%f"), RecoilAngle);
		//UE_LOG(LogTemp, Warning, TEXT("+++++++++++PitchDecayDelta+++++++++++:%f"), PitchDecayDelta);

		return -PitchDecayDelta;

	}
	
	return PitchDecayDelta;
}

void AGFHWeapon_Gun::TickRecoilCounterDown(float DeltaTime)
{

	if (mIsShooting)
	{
		mShootingTimer += DeltaTime;

		if (mShootingTimer > mShootingInterval)
		{
			mIsShooting = false;
		}
	}
	else
	{
		mRecoilCounter = mRecoilCounter - mRecoilCountDecaySpeed * DeltaTime;

		if (mRecoilCounter < 0)
		{
			mRecoilCounter = 0;
		}
	}

}

void AGFHWeapon_Gun::ApplyRecoilToCamera(FRotator CameraOffset)
{
	
	auto CurParasitifer = GetMyGFHCharParasitifer();

	if (CurParasitifer != nullptr)
	{

		auto CurCon = CurParasitifer->GetController();

		if (CurCon != nullptr)
		{
			FRotator CurRot = CurCon->GetControlRotation();
			CurRot += CameraOffset;
			CurCon->SetControlRotation(CurRot);
		}
		
	}
}

void AGFHWeapon_Gun::TickSpread(float DeltaTime)
{

	mCurSpread -= mSpreadDecaySpeed * DeltaTime;

	mCurSpread = FMath::Max(mCurSpread, mStandSpreadInitial);

}

// generate new recoil after one shot
FVector AGFHWeapon_Gun::GenNewRecoil()
{

	float NewRecoilSpeed_Pitch = GetNewRecoilSpeed(mMinPitch, mMaxPitch);
	float NewRecoilSpeed_Yaw = GetNewRecoilSpeed(mMinYaw, mMaxYaw);

	// if mRecoil_Speed_Pitch is zero,mRecoilSpeedPitchDir doesn't affect the result.
	//mRecoil_Speed_Pitch = mRecoil_Speed_Pitch * mRecoilSpeedPitchDir +  NewRecoilSpeed_Pitch;
	//mRecoil_Speed_Yaw = mRecoil_Speed_Yaw * mRecoilSpeedYawDir + NewRecoilSpeed_Yaw;

	mRecoil_Speed_Pitch = NewRecoilSpeed_Pitch;
	mRecoil_Speed_Yaw = NewRecoilSpeed_Yaw;

	//mRecoil_Speed_Pitch = FMath::Abs(NewRecoilSpeed_Pitch);
	//mRecoil_Speed_Yaw = FMath::Abs(NewRecoilSpeed_Yaw);

	if (GAllowRecoilDebug) {
		UE_LOG(LogTemp, Warning, TEXT("CurRecoil_Speed_Pitch:%f"), mRecoil_Speed_Pitch);

		UE_LOG(LogTemp, Warning, TEXT("CurRecoil_Speed_Yaw:%f"), mRecoil_Speed_Yaw);

		UE_LOG(LogTemp, Warning, TEXT("CurRecoilCounter:%f"), mRecoilCounter);
	}

	// To simplify the process of RecoilSpeed's decay, we introduce the
	// SpeedDir.It makes us ignoring the sign in the whole decay process,
	// just put the Dir to the last result.
	mRecoilSpeedPitchDir = FMath::Sign(mRecoil_Speed_Pitch);
	mRecoilSpeedYawDir = FMath::Sign(mRecoil_Speed_Yaw);

	mRecoil_Speed_Pitch = FMath::Abs(mRecoil_Speed_Pitch);
	mRecoil_Speed_Yaw = FMath::Abs(mRecoil_Speed_Yaw);

	return FVector(mRecoil_Speed_Pitch, mRecoil_Speed_Yaw, 0.0f);

}

float AGFHWeapon_Gun::GetNewRecoilSpeed(FVector4 MinFactor, FVector4 MaxFactor)
{

	float CounterValue = FMath::Floor(mRecoilCounter);
	float CounterSquare = FMath::Square(CounterValue);
	float CounterCube = FMath::Pow(CounterValue, 3.0f);

	float MinRecoilSpeed = MinFactor.X * CounterValue + MinFactor.Y * CounterSquare + MinFactor.Z * CounterCube + MinFactor.W;
	float MaxRecoilSpeed = MaxFactor.X * CounterValue + MaxFactor.Y * CounterSquare + MaxFactor.Z * CounterCube + MaxFactor.W;

	float NewRecoilSpeed = FMath::RandRange(MinRecoilSpeed, MaxRecoilSpeed);

	return NewRecoilSpeed;

}


// generate new spread after new shot
float AGFHWeapon_Gun::GenNewSpread()
{

	float NewPostureSpread = FMath::RandRange(mCurSpread * mSpreadMinFactor, mCurSpread);

	mCurSpread = NewPostureSpread + mStandSpreadFireAdd;

	if (mCurSpread > mStandSpreadMax)
	{
		mCurSpread = mStandSpreadMax;
	}
	//mCurSpread *= mSpreadSightsFactor;
	return mCurSpread;

}

float AGFHWeapon_Gun::GetCharSpeed() const
{

	auto CurParasitifer = GetMyGFHCharParasitifer();

	float CurCharSpeed = 0.0f;

	if (CurParasitifer != nullptr)
	{
		CurCharSpeed = CurParasitifer->GetVelocity().Size();
	}

	return CurCharSpeed;

}
void AGFHWeapon_Gun::RequestFire()
{
	if (GetGFHAbilitySystemComp())
	{

		UpdateCameraShotInfo();

		ServerSetCameraShotInfo(mCameraShotPos, mCameraShotDir);

		Super::RequestFire();

	}
}

void AGFHWeapon_Gun::UpdateCameraShotInfo()
{
	AGFHPlayerCharacter* MyPlayerParasitifer = GetMyGFHPlayerParasitifer();
	if (!MyPlayerParasitifer)
		return;

	auto CurController = MyPlayerParasitifer->GetMyGFHPlayerController();
	if (!CurController)
		return;

	// cache aim state is OpenGunScope?
	auto GTagMgr = UGFHEffectTagManager::Get(MyPlayerParasitifer->GetGameInstance());
	bool openAimScope = MyPlayerParasitifer->HasMatchingGameplayTag(GTagMgr->EffectAimTag());

	//------------------------CameraBeginPos----------------------

	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	FVector CameraPos = FVector::ZeroVector;
	FVector CameraDir = FVector::ZeroVector;

	CurController->GetViewportSize(ViewportSizeX, ViewportSizeY);
	CurController->DeprojectScreenPositionToWorld(ViewportSizeX * 0.5f, ViewportSizeY * 0.5f, CameraPos, CameraDir);

	//------------------------CameraShotPos-----------------------

	// get current spread with movement spread
	float CurCharSpeed = GetCharSpeed();

	float PosMoveSpread = mCurSpread + CurCharSpeed * mSpreadMovementFactor;// move spread == pose spread vaule + speedofgunner * movespreadfactor
	float FinalSpread = FMath::Min(PosMoveSpread, mMaxSpreadMovement);	    // final spread value == Min(move spread, mMaxSpreadMovement)

	int32 RandomSeed = FMath::Rand();
	FRandomStream SpreadStream(RandomSeed);
	float SpreadRadian = FMath::DegreesToRadians(FinalSpread);
	
	// switch truelyUsedSpreadSightsFactor by current open scopeState.
	FVector ShotDir;
	float truelyUsedSpreadSightsFactor = 1.0f;
	if (openAimScope) {
		truelyUsedSpreadSightsFactor = mSpreadSightsFactor * AdditionalSpreadFactor;
	} else {
		truelyUsedSpreadSightsFactor = 1.0f;
	}
	// calculate ShotDir within Cone
	ShotDir = SpreadStream.VRandCone(CameraDir, 
		SpreadRadian * mSpreadPitchFactor * truelyUsedSpreadSightsFactor, 
		SpreadRadian * mSpreadYawFactor * truelyUsedSpreadSightsFactor);

	//-----------------------------------------------------------------

	mCameraShotPos = CameraPos;
	mCameraShotDir = ShotDir;

}

void AGFHWeapon_Gun::OnFireMoved()
{
	mRecoil_Angle_Pitch = 0.0f;
	mRecoil_Angle_Yaw = 0.0f;
}

void AGFHWeapon_Gun::ServerSetCameraShotInfo_Implementation(FVector CameraShotPos, FVector CameraShotDir)
{
	mCameraShotPos = CameraShotPos;
	mCameraShotDir = CameraShotDir;
}

bool AGFHWeapon_Gun::ServerSetCameraShotInfo_Validate(FVector CameraShotPos, FVector CameraShotDir)
{
	return true;
}

UParticleSystem* AGFHWeapon_Gun::GetMuzzleFireEffect()
{

	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return mThirdPersonMuzzleFire;
	}

	return mFirstPersonMuzzleFire;

}

UParticleSystem* AGFHWeapon_Gun::GetShellCaseFireEffect()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return mThirdPersonShellCaseFire;
	}

	return mFirstPersonShellCaseFire;
}

UParticleSystem* AGFHWeapon_Gun::GetTrajectoryEffect()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return mThirdPersonTrajectory;
	}

	return mFirstPersonTrajectory;
}


USoundCue* AGFHWeapon_Gun::GetSingleShotSound()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return mThirdPersonSingleShot;
	}

	return mFirstPersonSingleShot;
}

USoundCue* AGFHWeapon_Gun::GetBurstSound()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		return mThirdPersonBurst;
	}

	return mFirstPersonBurst;
}
