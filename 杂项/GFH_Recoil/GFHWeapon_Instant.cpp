// Copyright GFH Game. All Rights Reserved.

#include "GFH.h"
#include "GFHAICharacter.h"
#include "GFHWeapon_Instant.h"
#include "Weapons/Debug/GFHAmmoPitDebug.h"


AGFHWeapon_Instant::AGFHWeapon_Instant()
{
	
}

void AGFHWeapon_Instant::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

bool AGFHWeapon_Instant::IsFiring() const
{
	auto EffectTagMgr = UGFHEffectTagManager::Get(GetGameInstance());

	if (!EffectTagMgr || !GetMyGFHCharParasitifer())
	{
		return false;
	}

	return GetMyGFHCharParasitifer()->HasMatchingGameplayTag(EffectTagMgr->EffectWeaponFireInstantTag());
}

float AGFHWeapon_Instant::GetDamage(FVector BeginPos, FVector EndPos)
{

	float Distance = FVector::Dist(BeginPos, EndPos);

	float EffectiveDamageDistance = GetEffectiveDamageDistance();
	float WeaponBaseAttack = GetWeaponBaseAttack();
	float MiniDamageDistance = GetMiniDamageDistance();
	float MiniDamage = GetMiniDamage();

	// return WeaponBaseAttack when the Distance is in the range of EffectiveDamageDistance
	if (Distance <= EffectiveDamageDistance)
	{
		return WeaponBaseAttack;
	}
	else
	{
		// if the Distance is in MiniDamageDistance and out of EffectiveDamageDistance
		if (Distance <= MiniDamageDistance)
		{
			// solve attenuation rate
			float AttenuationRate = (Distance - EffectiveDamageDistance) / (MiniDamageDistance - EffectiveDamageDistance);
			// solve attenuation
			float Attenuation = (WeaponBaseAttack - MiniDamage) * AttenuationRate;
			// based on Attenuation solve the remain damage
			float RemainDamage = WeaponBaseAttack - Attenuation;

			return RemainDamage;
		}
		// return MiniDamage when the Distance is out of MiniDamageDistance
		else
		{
			return MiniDamage;
		}
	}

	return 0.0f;
}

void AGFHWeapon_Instant::DoFire()
{
	AGFHPlayerCharacter* MyPlayerParasitifer = GetMyGFHPlayerParasitifer();
	if (!MyPlayerParasitifer)
		return;

	auto HitResult = DoLineTrance(MyPlayerParasitifer);

	if (MyPlayerParasitifer->IsAuthorityControlled())
	{
		if (mHasHitedAcotr && Cast<IGFHActorInterface>(HitResult.GetActor()))
		{
			auto GameInst = GetGameInstance();

			auto EffectTagMgr = UGFHEffectTagManager::Get(GameInst);
			auto GEMgr = UGFHGameplayEffectManager::Get(GameInst);

			if (GEMgr && EffectTagMgr)
			{
				FEffectSpec DeBuffSpec;
				DeBuffSpec.mInstigator = MyPlayerParasitifer;
				DeBuffSpec.mCauser = this;
				DeBuffSpec.mTargetActor = HitResult.GetActor();
				DeBuffSpec.mMagnitude = GetDamage(mBeginPos, HitResult.ImpactPoint);
				DeBuffSpec.mGEffectTag = EffectTagMgr->EffectInstantDamageTag();

				GEMgr->RequestApplyEffect(DeBuffSpec);
			}
		}
	}
	else
	{
		OnFireEnd();
	}
}

const FHitResult AGFHWeapon_Instant::DoLineTrance(const AGFHPlayerCharacter* MyPlayerParasitifer)
{
	FHitResult HitResult(ForceInit);
	FCollisionQueryParams CollisionQueryParam(FName(TEXT("Params")), true, 0);

	CollisionQueryParam.bTraceComplex = false;
	//CollisionQueryParam.bTraceAsyncScene = false;
	CollisionQueryParam.bReturnPhysicalMaterial = false;
	CollisionQueryParam.AddIgnoredActor(this);
	CollisionQueryParam.AddIgnoredActor(MyPlayerParasitifer);

	FCollisionObjectQueryParams ObjTypesWanted;
	ObjTypesWanted.AddObjectTypesToQuery(ECC_Pawn);
	ObjTypesWanted.AddObjectTypesToQuery(ECC_WorldStatic);

	//-------------------------camera------------------------

	mBeginPos = GetCameraShotPos();
	FVector CameraDir = GetCameraShotDir();

	const FVector& PosEnd = mBeginPos + CameraDir * 10000;

	//-------------------------------------------------------

	FCollisionShape CollShape = FCollisionShape::MakeCapsule(10, 5000);

	mHasHitedAcotr =
		GetWorld()->LineTraceSingleByObjectType(HitResult, mBeginPos, PosEnd, ObjTypesWanted, CollisionQueryParam);


	SetFinalPos(PosEnd);
	if (mHasHitedAcotr)
	{
		SetFinalPos(HitResult.ImpactPoint);
	}

	//if (CanDebugDraw())
	//{
	//	DrawDebugSphere(GetWorld(), HitResult.ImpactPoint, 3, 1, FColor(FMath::RandRange(50, 255), FMath::RandRange(50, 255), FMath::RandRange(50, 255)), false, 5, 0, 1);
	//}

	// Load Ammo Pit Class and check for next logic phase
	UClass* BlueprintVar = StaticLoadClass(AGFHAmmoPitDebug::StaticClass(), nullptr,
		TEXT("Blueprint'/Game/Blueprints/Weapons/Rifle/DebugWeapon/AmmoPitDebug_1.AmmoPitDebug_1_C'"));
	if (BlueprintVar != nullptr) {
		// spawn the BP instance in the scene.
		const FTransform Transform = FTransform(FVector(HitResult.ImpactPoint));

		AGFHAmmoPitDebug* pAmmoPit = GetWorld()->SpawnActor<AGFHAmmoPitDebug>(BlueprintVar, Transform);
		if (pAmmoPit != nullptr) {

		}
	}

	/// these are debug geometry algorithms by using in-constructed APIS
	//GetWorld()->SweepSingleByObjectType(HitResult, BeginPos, PosEnd, FQuat::Identity, ObjTypesWanted, CollShape, CollisionQueryParam);

	//DrawDebugLine(GetWorld(), BeginPos, PosEnd, FColor::Green, false, 10, 0, 0.02);

	//DrawDebugSphere(GetWorld(), HitResult.ImpactPoint, 3, 30, FColor(FMath::RandRange(50, 255), FMath::RandRange(50, 255), FMath::RandRange(50, 255)), false, 5, 0, 1);

	//DrawDebugCapsule(this->GetWorld(), (BeginPos + PosEnd) * 0.5f, 5000, 10, FQuat::Identity, FColor::Red, false, 5.0f);

	return HitResult;
}
