// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "GFHWeapon.h"
#include "GFHWeapon_Gun.generated.h"


UCLASS(Abstract)
class GFH_API AGFHWeapon_Gun : public AGFHWeapon
{
	GENERATED_BODY()
protected:
	//
	AGFHWeapon_Gun();

public:

	virtual void BeginPlay() override;
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "GFHWeapon|Effect")
		UParticleSystem* GetMuzzleFireEffect();

	UFUNCTION(BlueprintCallable, Category = "GFHWeapon|Effect")
		UParticleSystem* GetShellCaseFireEffect();

	UFUNCTION(BlueprintCallable, Category = "GFHWeapon|Effect")
		UParticleSystem* GetTrajectoryEffect();

	UFUNCTION(BlueprintCallable, Category = "GFHWeapon|Audio")
		USoundCue* GetSingleShotSound();

	UFUNCTION(BlueprintCallable, Category = "GFHWeapon|Audio")
		USoundCue* GetBurstSound();

	/***/
	UFUNCTION(Server, reliable, WithValidation)
		void ServerSetCameraShotInfo(FVector CameraShotPos, FVector CameraShotDir);

public:

	float GetEffectiveDamageDistance() const
	{
		return mEffectiveDamageDistance;
	}

	float GetWeaponBaseAttack() const
	{
		return mWeaponBaseAttack;
	}

	float GetMiniDamageDistance() const
	{
		return mMiniDamageDistance;
	}

	float GetMiniDamage() const
	{
		return mMiniDamage;
	}

public:

	void OnFireEnd();

	// Caculate recoil angle and speed
	FVector GenNewRecoil();

	//update spread after shot
	float GenNewSpread();

	float CalculateSpread(float CurSpread, float SpreadDirFactor);

	void TickRecoil(float DeltaTime);

	void TickRecoilAngleUp(float DeltaTime);

	float IncreaseRecoilAngle(float& CurSpeed, float& CurAngle, float SpeedDir, float MaxRecoil, float DeltaTime);

	void TickRecoilAngleDown(float DeltaTime);

	float DecreaseRecoilAngle(float& RecoilSpeed, float& RecoilAngle, float SpeedDir, float Threshold, float DeltaTime);

	void TickRecoilCounterDown(float DeltaTime);

	void ApplyRecoilToCamera(FRotator CameraOffset);

	void TickSpread(float DeltaTime);

	float GetNewRecoilSpeed(FVector4 MinFactor, FVector4 MaxFactor);

	float GetCharSpeed() const;

	virtual void RequestFire() override;

	void UpdateCameraShotInfo();

	// on player moved camera in firing
	void OnFireMoved();

	FVector GetCameraShotPos()
	{
		return mCameraShotPos;
	}

	FVector GetCameraShotDir()
	{
		return mCameraShotDir;
	}

private:

	//------RecoilParam-----

	float mRecoil_Angle_Pitch;

	float mRecoil_Speed_Pitch;

	float mRecoil_Angle_Yaw;

	float mRecoil_Speed_Yaw;

	// we need float type for decay reason
	// to subtract delta time
	float mRecoilCounter;

	//-----SpreadParam------

	float mCurSpread;

	// SpreadDecaySpeed varies according to context,so we use
	// mSpreadDecaySpeed to cover Stand,Movement,Jump DecaySpeed
	float mSpreadDecaySpeed;

	float mSpreadMinFactor;

	float mShootingInterval;

	float mShootingTimer;

	bool mIsShooting;

	float mRecoilSpeedPitchDir;

	float mRecoilSpeedYawDir;

	FVector mCameraShotPos;

	FVector mCameraShotDir;

private:

	//-----------------GeneralParam-----------------
	
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|GeneralParam", meta = (AllowPrivateAccess = "true"))
		int32 mWeaponID;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|GeneralParam", meta = (AllowPrivateAccess = "true"))
		FName mWeaponName;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|GeneralParam", meta = (AllowPrivateAccess = "true"))
		FGameplayTag mWeaponType;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|GeneralParam", meta = (AllowPrivateAccess = "true"))
		float mWeaponBaseAttack;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|GeneralParam", meta = (AllowPrivateAccess = "true"))
		float mWeaponWeight;

	//-----------------RemoteParam-----------------
	 
	/** default bullets num for one clip */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		int32 mClipDefaultBulletsNum;

	/** backup bullets num，-1 means infinite */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		int32 mBackupBulletsNum;

	/** weapon's shooting mode(1.auto 2.three shot 3.single) */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		FGameplayTag mShootingMode;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mShootingSpeed;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mEffectiveDamageDistance;

	/** attenuate to mini damage distance */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mMiniDamageDistance;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mMiniDamage;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mCrosshairSize;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RemoteParam", meta = (AllowPrivateAccess = "true"))
		float mReloadEffectiveTime;


	//-----------------RecoilParam-----------------
	
	/** mini vertical recoil param */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		FVector4 mMinPitch;

	/** max vertical recoil param */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		FVector4 mMaxPitch;

	/** mini horizontal recoil param */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		FVector4 mMinYaw;

	/** max horizontal recoil param */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		FVector4 mMaxYaw;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		int32 mRecoilCountMax;

	/** RecoilCountDecaySpeed，one / second */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilCountDecaySpeed;

	/** recoil return acc */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilReturnAcc;

	/** recoil return acc factor */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilReturnAccFactor;

	/** vertical recoil pitch min angle */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilPitchMin;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilPitchMax;

	/** horizontal recoil yaw angle */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilYawMin;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilYawMax;

	/** RecoilSights change rate */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|RecoilParam", meta = (AllowPrivateAccess = "true"))
		float mRecoilSightsFactor;

	//-----------------SpreadParam-----------------

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mStandSpreadInitial;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mStandSpreadFireAdd;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mStandSpreadMax;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mStandSpreadMinFactor;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mStandSpreadDecaySpeed;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadPitchFactor;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadYawFactor;

	//--------SpreadMovementParams-------

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadMovementFactor;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mMaxSpreadMovement;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadMovementDecaySpeed;

	//----------SpreadJumpParams---------
	
	/** jump or for vary rate */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadJumpFactor;

	/** jump or fall max spread */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadJumpMax;

	/** jump or fall spread decay speed */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadJumpDecaySpeed;

	//----------SpreadSightsParam--------

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|SpreadParam", meta = (AllowPrivateAccess = "true"))
		float mSpreadSightsFactor;

	//-----------------EffectParam-----------------

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mFirstPersonMuzzleFire;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mThirdPersonMuzzleFire;

	/** first person shell case fire effect(hi - tech weapon may config as null) */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mFirstPersonShellCaseFire;

	/** third person shell case fire effect（hi - tech weapon may config as null） */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mThirdPersonShellCaseFire;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mFirstPersonTrajectory;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		UParticleSystem* mThirdPersonTrajectory;

	/** first person bullet effect speed(meter / second) */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		float mFirstPersonBulletEffectSpeed;

	/** third person bullet effect speed(meter / second) */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		float mThirdPersonBulletEffectSpeed;

	/** bullet shooting frequency(how many bullets can generate one effect) */
	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|EffectParam", meta = (AllowPrivateAccess = "true"))
		float mEffectFireRate;

	//-----------------AudioParam-----------------

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|AudioParam", meta = (AllowPrivateAccess = "true"))
		USoundCue* mFirstPersonSingleShot;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|AudioParam", meta = (AllowPrivateAccess = "true"))
		USoundCue* mThirdPersonSingleShot;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|AudioParam", meta = (AllowPrivateAccess = "true"))
		USoundCue* mFirstPersonBurst;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|AudioParam", meta = (AllowPrivateAccess = "true"))
		USoundCue* mThirdPersonBurst;

	UPROPERTY(EditDefaultsOnly, Category = "GFHWeapon|AudioParam", meta = (AllowPrivateAccess = "true"))
		float mSoundRadius;
};
