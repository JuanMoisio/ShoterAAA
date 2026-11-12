#include "Core/ShoterArmRig.h"
#include "ShoterAAA.h"
#include "AnimationRuntime.h"

namespace
{
	/** Orthonormal frame with the X axis along Primary and Y as close as possible to Secondary. */
	FQuat BasisXY(const FVector& Primary, const FVector& Secondary)
	{
		return FRotationMatrix::MakeFromXY(Primary, Secondary).ToQuat();
	}

	/** Frame (X = forward, Y = palm normal x forward, Z = palm normal). Forward and PalmNormal need not be exactly perpendicular. */
	FQuat HandFrame(const FVector& Forward, const FVector& PalmNormal)
	{
		FVector F = Forward.GetSafeNormal();
		FVector P = PalmNormal - F * FVector::DotProduct(PalmNormal, F);
		if (P.SizeSquared() < KINDA_SMALL_NUMBER)
		{
			P = FMath::Abs(F.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
			P = P - F * FVector::DotProduct(P, F);
		}
		return FRotationMatrix::MakeFromXZ(F, P.GetSafeNormal()).ToQuat();
	}

	FName ArmBone(const TCHAR* Base, bool bRight)
	{
		return FName(*(FString(Base) + (bRight ? TEXT("_r") : TEXT("_l"))));
	}
}

bool FShoterArmRig::Init(const FReferenceSkeleton& Ref, bool bRightArm)
{
	bValid = false;
	bRight = bRightArm;
	Digits.Reset();
	Twists.Reset();
	Driven.Reset();

	Upper = Ref.FindBoneIndex(ArmBone(TEXT("upperarm"), bRight));
	Lower = Ref.FindBoneIndex(ArmBone(TEXT("lowerarm"), bRight));
	Hand = Ref.FindBoneIndex(ArmBone(TEXT("hand"), bRight));
	if (Upper == INDEX_NONE || Lower == INDEX_NONE || Hand == INDEX_NONE)
	{
		return false;
	}

	const TArray<FTransform>& RefLocal = Ref.GetRefBonePose();
	auto RefCS = [&](int32 Bone) { return FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Bone); };

	const FTransform UpperCS = RefCS(Upper);
	const FTransform LowerCS = RefCS(Lower);
	const FTransform HandCS = RefCS(Hand);
	RefShoulder = UpperCS.GetLocation();
	RefUpperRot = UpperCS.GetRotation();
	RefLowerRot = LowerCS.GetRotation();
	RefHandRot = HandCS.GetRotation();

	const FVector UpperDir = (LowerCS.GetLocation() - UpperCS.GetLocation()).GetSafeNormal();
	const FVector LowerDir = (HandCS.GetLocation() - LowerCS.GetLocation()).GetSafeNormal();
	UpperLength = static_cast<float>((LowerCS.GetLocation() - UpperCS.GetLocation()).Size());
	LowerLength = static_cast<float>((HandCS.GetLocation() - LowerCS.GetLocation()).Size());

	// The hinge axis is the normal of the plane the (slightly bent) reference arm lies in; both bones share it.
	FVector Hinge = FVector::CrossProduct(UpperDir, LowerDir);
	if (Hinge.SizeSquared() < 1e-6)
	{
		Hinge = FVector::CrossProduct(UpperDir, FVector::ForwardVector);
	}
	Hinge = Hinge.GetSafeNormal();
	UpperBasisInv = BasisXY(RefUpperRot.UnrotateVector(UpperDir), RefUpperRot.UnrotateVector(Hinge)).Inverse();
	LowerBasisInv = BasisXY(RefLowerRot.UnrotateVector(LowerDir), RefLowerRot.UnrotateVector(Hinge)).Inverse();
	LowerToHandRef = RefLowerRot.Inverse() * RefHandRot;

	// Digits.
	struct FDigitDef { const TCHAR* Name; bool bMetacarpal; bool bThumb; float Scale; };
	static const FDigitDef Defs[] =
	{
		{ TEXT("index"), true, false, 0.92f },
		{ TEXT("middle"), true, false, 1.f },
		{ TEXT("ring"), true, false, 1.05f },
		{ TEXT("pinky"), true, false, 1.1f },
		{ TEXT("thumb"), false, true, 1.f },
	};

	FVector FingerSum = FVector::ZeroVector;
	int32 FingerCount = 0;
	const FVector HandPos = HandCS.GetLocation();
	TArray<FVector, TInlineAllocator<5>> KnucklePositions;
	// First pass: the reference hand frame from the knuckles.
	for (const FDigitDef& Def : Defs)
	{
		if (Def.bThumb)
		{
			continue;
		}
		const int32 Knuckle = Ref.FindBoneIndex(ArmBone(*(FString(Def.Name) + TEXT("_01")), bRight));
		if (Knuckle != INDEX_NONE)
		{
			FingerSum += RefCS(Knuckle).GetLocation();
			++FingerCount;
		}
	}
	if (FingerCount == 0)
	{
		return false;
	}
	const FVector KnuckleCentre = FingerSum / FingerCount;
	const FVector RefFingers = (KnuckleCentre - HandPos).GetSafeNormal();
	// In the reference (A) pose the palm faces the thigh: towards the body centre line (mesh X = 0).
	const FVector RefPalmNormal(bRight ? 1.f : -1.f, 0.f, 0.f);
	HandFrameRefInv = HandFrame(RefFingers, RefPalmNormal).Inverse();
	const FVector RefPalmDir = (RefPalmNormal - RefFingers * FVector::DotProduct(RefPalmNormal, RefFingers)).GetSafeNormal();

	// Palm centre: between the wrist and the knuckles.
	PalmLocal = RefHandRot.UnrotateVector((HandPos + KnuckleCentre) * 0.5 - HandPos);

	for (const FDigitDef& Def : Defs)
	{
		FDigit Digit;
		Digit.bThumb = Def.bThumb;
		if (Def.bMetacarpal)
		{
			const int32 Meta = Ref.FindBoneIndex(ArmBone(*(FString(Def.Name) + TEXT("_metacarpal")), bRight));
			if (Meta != INDEX_NONE)
			{
				Digit.Bones.Add(Meta);
				Digit.CurlDegrees.Add(0.f);
			}
		}
		int32 First = INDEX_NONE;
		int32 Last = INDEX_NONE;
		for (int32 Joint = 1; Joint <= 3; ++Joint)
		{
			const int32 Bone = Ref.FindBoneIndex(ArmBone(*FString::Printf(TEXT("%s_0%d"), Def.Name, Joint), bRight));
			if (Bone == INDEX_NONE)
			{
				break;
			}
			if (First == INDEX_NONE)
			{
				First = Bone;
			}
			Last = Bone;
			Digit.Bones.Add(Bone);
			static const float FingerDegrees[3] = { 55.f, 75.f, 50.f };
			static const float ThumbDegrees[3] = { 14.f, 24.f, 26.f };
			Digit.CurlDegrees.Add((Def.bThumb ? ThumbDegrees[Joint - 1] : FingerDegrees[Joint - 1]) * Def.Scale);
		}
		if (First == INDEX_NONE || Last == First)
		{
			continue;
		}

		// Curl axis: perpendicular to the finger and to the palm normal, signed so the tip moves towards the palm.
		const FVector Direction = (RefCS(Last).GetLocation() - RefCS(First).GetLocation()).GetSafeNormal();
		FVector Axis = FVector::CrossProduct(Direction, RefPalmDir).GetSafeNormal();
		for (const int32 Bone : Digit.Bones)
		{
			Digit.RefLocalRot.Add(RefLocal[Bone].GetRotation());
			Digit.RefLocalLoc.Add(RefLocal[Bone].GetLocation());
			Digit.CurlAxisLocal.Add(RefCS(Bone).GetRotation().UnrotateVector(Axis));
			Driven.AddUnique(Bone);
		}
		Digits.Add(MoveTemp(Digit));
	}

	// Twist bones.
	auto AddTwists = [&](const TCHAR* Base, bool bLowerArm)
	{
		for (int32 Index = 1; Index <= 3; ++Index)
		{
			const int32 Bone = Ref.FindBoneIndex(ArmBone(*FString::Printf(TEXT("%s_twist_0%d"), Base, Index), bRight));
			if (Bone == INDEX_NONE)
			{
				continue;
			}
			FTwist Twist;
			Twist.Bone = Bone;
			Twist.bOnLowerArm = bLowerArm;
			Twist.RefLocalRot = RefLocal[Bone].GetRotation();
			Twist.RefLocalLoc = RefLocal[Bone].GetLocation();
			if (bLowerArm)
			{
				Twist.Fraction = FMath::Clamp(static_cast<float>((RefCS(Bone).GetLocation() - LowerCS.GetLocation()).Size()) / FMath::Max(LowerLength, 1.f), 0.f, 1.f);
			}
			Twists.Add(Twist);
			Driven.AddUnique(Bone);
		}
	};
	AddTwists(TEXT("upperarm"), false);
	AddTwists(TEXT("lowerarm"), true);

	Driven.AddUnique(Upper);
	Driven.AddUnique(Lower);
	Driven.AddUnique(Hand);
	bValid = true;
	return true;
}

FVector FShoterArmRig::Solve(const FShoterArmTarget& Target, TArray<FTransform>& CS, bool bKeepShoulder) const
{
	if (!bValid || !CS.IsValidIndex(Hand))
	{
		return FVector::ZeroVector;
	}

	FVector Shoulder = bKeepShoulder ? CS[Upper].GetLocation() : Target.Shoulder;

	// --- Hand ---
	const FQuat HandRot = HandFrame(Target.Fingers, Target.PalmNormal) * HandFrameRefInv * RefHandRot;
	FVector Wrist = Target.Palm - HandRot.RotateVector(PalmLocal * Target.HandScale);

	// --- Reach ---
	const double MaxReach = 0.985 * (UpperLength + LowerLength);
	const double MinReach = FMath::Abs(UpperLength - LowerLength) * 1.1 + 1.0;
	FVector ToWrist = Wrist - Shoulder;
	double Distance = ToWrist.Size();
	if (Distance > MaxReach && Distance > KINDA_SMALL_NUMBER)
	{
		if (Target.bPullShoulder && !bKeepShoulder)
		{
			Shoulder = Wrist - ToWrist / Distance * MaxReach;
		}
		else
		{
			Wrist = Shoulder + ToWrist / Distance * MaxReach;
		}
		ToWrist = Wrist - Shoulder;
		Distance = MaxReach;
	}
	else if (Distance < MinReach)
	{
		const FVector Direction = Distance > KINDA_SMALL_NUMBER ? ToWrist / Distance : FVector::ForwardVector;
		Wrist = Shoulder + Direction * MinReach;
		ToWrist = Wrist - Shoulder;
		Distance = MinReach;
	}
	const FVector Chord = ToWrist / Distance;

	// --- Elbow ---
	FVector Pole = Target.ElbowPole - Chord * FVector::DotProduct(Target.ElbowPole, Chord);
	if (Pole.SizeSquared() < 1e-6)
	{
		Pole = FVector::DownVector - Chord * FVector::DotProduct(FVector::DownVector, Chord);
		if (Pole.SizeSquared() < 1e-6)
		{
			Pole = FVector::RightVector;
		}
	}
	Pole = Pole.GetSafeNormal();
	const double A = (UpperLength * UpperLength - LowerLength * LowerLength + Distance * Distance) / (2.0 * Distance);
	const double H = FMath::Sqrt(FMath::Max(UpperLength * UpperLength - A * A, 0.0));
	const FVector Elbow = Shoulder + Chord * A + Pole * H;

	// --- Bone orientations: bone axis along the segment, hinge axis normal to the plane shoulder-elbow-wrist (bent towards the pole) ---
	const FVector UpperDir = (Elbow - Shoulder).GetSafeNormal();
	const FVector LowerDir = (Wrist - Elbow).GetSafeNormal();
	FVector Hinge = FVector::CrossProduct(Pole, Chord);
	Hinge = (Hinge - UpperDir * FVector::DotProduct(Hinge, UpperDir)).GetSafeNormal();
	FVector HingeLower = (Hinge - LowerDir * FVector::DotProduct(Hinge, LowerDir)).GetSafeNormal();
	const FQuat UpperRot = BasisXY(UpperDir, Hinge) * UpperBasisInv;
	const FQuat LowerRot = BasisXY(LowerDir, HingeLower) * LowerBasisInv;

	const FVector Slim(1.f, Target.LimbThickness, Target.LimbThickness);
	CS[Upper] = FTransform(UpperRot, Shoulder, Slim);
	CS[Lower] = FTransform(LowerRot, Elbow, Slim);
	CS[Hand] = FTransform(HandRot, Wrist, FVector(Target.HandScale));

	// --- Twist bones ---
	FQuat WristTwist = FQuat::Identity;
	{
		const FQuat Delta = HandRot * LowerToHandRef.Inverse() * LowerRot.Inverse();
		FQuat Swing;
		FQuat Twist;
		Delta.ToSwingTwist(LowerDir, Swing, Twist);
		WristTwist = Twist;
	}
	for (const FTwist& Twist : Twists)
	{
		const FTransform& Parent = CS[Twist.bOnLowerArm ? Lower : Upper];
		FQuat Rot = Parent.GetRotation() * Twist.RefLocalRot;
		if (Twist.bOnLowerArm)
		{
			Rot = FQuat::Slerp(FQuat::Identity, WristTwist, Twist.Fraction) * Rot;
		}
		CS[Twist.Bone] = FTransform(Rot, Parent.TransformPosition(Twist.RefLocalLoc), Slim);
	}

	// --- Fingers ---
	for (const FDigit& Digit : Digits)
	{
		const float Curl = Digit.bThumb ? Target.ThumbCurl : Target.FingerCurl;
		FTransform ParentCS = CS[Hand];
		for (int32 Index = 0; Index < Digit.Bones.Num(); ++Index)
		{
			FQuat LocalRot = Digit.RefLocalRot[Index];
			const float Degrees = Digit.CurlDegrees[Index] * Curl;
			if (Degrees > KINDA_SMALL_NUMBER)
			{
				LocalRot = LocalRot * FQuat(Digit.CurlAxisLocal[Index], FMath::DegreesToRadians(Degrees));
			}
			const FTransform Local(LocalRot, Digit.RefLocalLoc[Index]);
			CS[Digit.Bones[Index]] = Local * ParentCS;
			ParentCS = CS[Digit.Bones[Index]];
		}
	}
	return Shoulder;
}

void FShoterArmRig::CollapseAll(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS, const FVector& Point)
{
	OutCS.SetNum(Ref.GetNum());
	const FTransform Collapsed(FQuat::Identity, Point, FVector(0.001));
	for (FTransform& Transform : OutCS)
	{
		Transform = Collapsed;
	}
}

void FShoterArmRig::ToLocalSpace(const FReferenceSkeleton& Ref, const TArray<FTransform>& CS, TArray<FTransform>& OutLocal)
{
	OutLocal.SetNum(Ref.GetNum());
	for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
	{
		const int32 Parent = Ref.GetParentIndex(Bone);
		OutLocal[Bone] = Parent == INDEX_NONE ? CS[Bone] : CS[Bone].GetRelativeTransform(CS[Parent]);
	}
}
