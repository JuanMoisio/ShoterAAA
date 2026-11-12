// Procedural pose of one mannequin arm: analytic two-bone IK to a palm target, hand frame, twist distribution and curled fingers.
// Everything works on component-space (mesh space) transforms, so the same rig drives the first-person arms (a poseable mesh)
// and the left arm of the third-person body while it reloads.
#pragma once

#include "CoreMinimal.h"
#include "ReferenceSkeleton.h"

/** Where an arm has to go. All vectors are in the mesh space of the skeleton (the mannequin faces +Y, its left is +X). */
struct FShoterArmTarget
{
	/** Shoulder position (the upper arm bone origin). Ignored when the solve keeps the shoulder that is already in the pose. */
	FVector Shoulder = FVector::ZeroVector;
	/** Pull the shoulder towards the hand when the target is out of reach, so the arm never stretches and the hand never lets go. */
	bool bPullShoulder = false;

	/** Centre of the palm. */
	FVector Palm = FVector::ZeroVector;
	/** Direction the extended fingers point. */
	FVector Fingers = FVector::ForwardVector;
	/** Direction the palm faces (the side the fingers curl towards). */
	FVector PalmNormal = FVector::UpVector;
	/** Direction (not a point) the elbow bulges towards. */
	FVector ElbowPole = FVector::DownVector;

	/** 0 = open hand as in the reference pose, 1 = closed round a grip. */
	float FingerCurl = 0.f;
	float ThumbCurl = 0.f;
	/** Cross-section scale of the upper arm, forearm and twist bones (1 = mesh as authored). */
	float LimbThickness = 1.f;
	/** Uniform scale of hand and fingers (the first-person hands are drawn a little bigger so they read as hands). */
	float HandScale = 1.f;
};

class SHOTERAAA_API FShoterArmRig
{
public:
	/** Caches the bones and reference data of the right or left arm. Returns false when the skeleton does not look like the mannequin. */
	bool Init(const FReferenceSkeleton& Ref, bool bRightArm);
	bool IsValid() const { return bValid; }

	/**
	 * Writes the component-space transforms of the upper arm, forearm, hand, twist bones and fingers into InOutCS (which must have one
	 * entry per bone). Other bones are not touched. With bKeepShoulder the shoulder is taken from the pose already in InOutCS.
	 * Returns the shoulder that was used.
	 */
	FVector Solve(const FShoterArmTarget& Target, TArray<FTransform>& InOutCS, bool bKeepShoulder) const;

	/** Every bone Solve() writes (arm chain, twist bones, fingers). */
	const TArray<int32>& GetDrivenBones() const { return Driven; }
	int32 GetUpperArmBone() const { return Upper; }
	int32 GetHandBone() const { return Hand; }
	float GetReach() const { return UpperLength + LowerLength; }

	/** Fills every bone with a collapsed (tiny, positioned at Point) transform: what is not an arm must not be seen. */
	static void CollapseAll(const FReferenceSkeleton& Ref, TArray<FTransform>& OutCS, const FVector& Point);

	/** Converts component-space transforms to the local (parent relative) ones a poseable mesh wants. */
	static void ToLocalSpace(const FReferenceSkeleton& Ref, const TArray<FTransform>& CS, TArray<FTransform>& OutLocal);

private:
	struct FDigit
	{
		TArray<int32, TInlineAllocator<4>> Bones;	// metacarpal (optional, never curled), 01, 02, 03
		TArray<FQuat, TInlineAllocator<4>> RefLocalRot;
		TArray<FVector, TInlineAllocator<4>> RefLocalLoc;
		TArray<FVector, TInlineAllocator<4>> CurlAxisLocal;	// curl axis in the bone's own frame
		TArray<float, TInlineAllocator<4>> CurlDegrees;		// per bone at full curl (0 for the metacarpal)
		bool bThumb = false;
	};

	struct FTwist
	{
		int32 Bone = INDEX_NONE;
		bool bOnLowerArm = false;
		FQuat RefLocalRot = FQuat::Identity;
		FVector RefLocalLoc = FVector::ZeroVector;
		float Fraction = 0.f;	// share of the wrist twist this bone takes (0 at the elbow, 1 at the wrist)
	};

	bool bValid = false;
	bool bRight = true;
	int32 Upper = INDEX_NONE;
	int32 Lower = INDEX_NONE;
	int32 Hand = INDEX_NONE;
	float UpperLength = 0.f;
	float LowerLength = 0.f;

	FQuat RefUpperRot = FQuat::Identity;
	FQuat RefLowerRot = FQuat::Identity;
	FQuat RefHandRot = FQuat::Identity;
	FQuat UpperBasisInv = FQuat::Identity;	// inverse of the upper arm's (bone axis, hinge axis) frame in its own space
	FQuat LowerBasisInv = FQuat::Identity;
	FQuat HandFrameRefInv = FQuat::Identity;	// inverse of the reference hand frame (fingers, palm normal)
	FQuat LowerToHandRef = FQuat::Identity;	// RefLowerRot^-1 * RefHandRot
	FVector PalmLocal = FVector::ZeroVector;	// wrist to palm centre in hand bone space
	FVector RefShoulder = FVector::ZeroVector;

	TArray<FDigit> Digits;
	TArray<FTwist> Twists;
	TArray<int32> Driven;
};
