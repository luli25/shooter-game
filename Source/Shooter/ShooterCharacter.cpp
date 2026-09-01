// Fill out your copyright notice in the Description page of Project Settings.


#include "ShooterCharacter.h"

#include "DrawDebugHelpers.h"
#include "Camera/CameraComponent.h"
#include "Engine/SkeletalMeshSocket.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Particles/ParticleSystemComponent.h"

// Sets default values
AShooterCharacter::AShooterCharacter() :
    BaseTurnRate(45.0f),
    BaseLookUpRate(45.0f),
    bAiming(false),
    DefaultCameraFOV(0.0f),
    ZoomedCameraFOV(40.0f),
    CurrentCameraFOV(0.0f),
    ZoomInterpSpeed(20.0f),

	CrosshairSpreadMultiplier(0.0f),
	CrosshairVelocityFactor(0.0f),
	CrosshairInAirFactor(0.0f),
	CrosshairAimFactor(0.0f),
	CrosshairShootingFactor(0.0f),

    AutomaticFireRate(0.1f),
    bShouldFire(true),
    bFireButtonPressed(false),

    ShootTimeDuration(0.05f),
	bIsFiringBullet(false)

{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 180.0f;
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->SocketOffset = FVector(0.0f, 50.0f, 70.0f);

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Don't rotate when the controller rotates. The controller only affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = false; // Character moves in the direction of input...
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); //... at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.0f;
	GetCharacterMovement()->AirControl = 0.2f;
}

// Called when the game starts or when spawned
void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (FollowCamera)
	{
		DefaultCameraFOV = GetFollowCamera()->FieldOfView;
		CurrentCameraFOV = DefaultCameraFOV;
	}
}

void AShooterCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// Find out which is forward
		const FRotator Rotator{ Controller->GetControlRotation() };
		const FRotator YawRotator{ Rotator.Yaw };

		const FVector Direction{ FRotationMatrix{YawRotator}.GetUnitAxis(EAxis::X) };
		AddMovementInput(Direction, Value);
	}
}

void AShooterCharacter::MoveRight(float Value)
{
	// Find out which is right
	const FRotator Rotator{ Controller->GetControlRotation() };
	const FRotator YawRotator{ Rotator.Yaw };

	const FVector Direction{ FRotationMatrix{YawRotator}.GetUnitAxis(EAxis::Y) };
	AddMovementInput(Direction, Value);
}

void AShooterCharacter::TurnAtRate(float Rate)
{
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AShooterCharacter::LookUpRate(float Rate)
{
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AShooterCharacter::FireWeapon()
{
	PlayFireSound();
	SendBullet();
	PlayHipFireMontage();
	StartCrosshairBulletFire();
}

void AShooterCharacter::AimingButtonPressed()
{
	bAiming = true;
}

void AShooterCharacter::AimingButtonReleased()
{
	bAiming = false;
}

void AShooterCharacter::FireButtonPressed()
{
	bFireButtonPressed = true;
	StartFireTimer();
}

void AShooterCharacter::FireButtonReleased()
{
	bFireButtonPressed = false;
}

void AShooterCharacter::StartFireTimer()
{
	if (bShouldFire)
	{
		FireWeapon();
		bShouldFire = false;

		GetWorldTimerManager().SetTimer(
			AutoFireTimer,
			this,
			&AShooterCharacter::AutoFireReset,
			AutomaticFireRate);
	}
}

void AShooterCharacter::AutoFireReset()
{
	bShouldFire = true;

	if (bFireButtonPressed)
	{
		StartFireTimer();
	}
}

void AShooterCharacter::PlayFireSound()
{
	if (FireSound)
	{
		UGameplayStatics::PlaySound2D(this, FireSound);
	}
}

void AShooterCharacter::SendBullet()
{
	const USkeletalMeshSocket* BarrelSocket = GetMesh()->GetSocketByName("barrelSocket");

	if (!BarrelSocket)
	{
		return;
	}

	const FTransform SocketTransform = BarrelSocket->GetSocketTransform(GetMesh());

	if (MuzzleFlash)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), MuzzleFlash, SocketTransform);
	}

	FVector BeamEndPoint;
	const bool bBeamEnd = GetBeamEndLocation(SocketTransform.GetLocation(), BeamEndPoint);

	if (!bBeamEnd)
	{
		return;
	}

	if (ImpactParticles)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ImpactParticles, BeamEndPoint);
	}

	if (BeamParticles)
	{
		UParticleSystemComponent* Beam = UGameplayStatics::SpawnEmitterAtLocation(
			GetWorld(),
			BeamParticles,
			SocketTransform);

		if (Beam)
		{
			Beam->SetVectorParameter(FName("Target"), BeamEndPoint);
		}
	}
}

bool AShooterCharacter::GetBeamEndLocation(const FVector& MuzzleSocketLocation, FVector& OutBeamLocation)
{
	FVector2D ViewportSize;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	FVector2D CrosshairLocation(ViewportSize.X / 2, ViewportSize.Y / 2);

	FVector CrosshairWorldPosition;
	FVector CrosshairWorldDirection;

	const bool bScreenToWorld = UGameplayStatics::DeprojectScreenToWorld(
		UGameplayStatics::GetPlayerController(this, 0),
		CrosshairLocation,
		CrosshairWorldPosition,
		CrosshairWorldDirection);

	if (!bScreenToWorld)
	{
		return false;
	}

	FHitResult ScreenTraceHit;
	const FVector Start{ CrosshairWorldPosition };
	const FVector End{ CrosshairWorldPosition + CrosshairWorldDirection * 50'000.0f };

	OutBeamLocation = End;

	GetWorld()->LineTraceSingleByChannel(
		ScreenTraceHit,
		Start,
		End,
		ECollisionChannel::ECC_Visibility);

	if (ScreenTraceHit.bBlockingHit)
	{
		OutBeamLocation = ScreenTraceHit.Location;
	}

	FHitResult WeaponHitTrace;
	const FVector WeaponTraceStart{ MuzzleSocketLocation };
	const FVector WeaponTraceEnd{ OutBeamLocation };

	GetWorld()->LineTraceSingleByChannel(
		WeaponHitTrace,
		WeaponTraceStart,
		WeaponTraceEnd,
		ECollisionChannel::ECC_Visibility);

	if (WeaponHitTrace.bBlockingHit)
	{
		OutBeamLocation = WeaponHitTrace.Location;
	}

	return true;
}

void AShooterCharacter::PlayHipFireMontage()
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();

	if (AnimInstance && HipFireMontage)
	{
		AnimInstance->Montage_Play(HipFireMontage);
		AnimInstance->Montage_JumpToSection(FName("StartFire"));
	}
}

void AShooterCharacter::CameraInterpZoom(float DeltaTime)
{
	if (bAiming)
	{
		CurrentCameraFOV = FMath::FInterpTo(
			CurrentCameraFOV,
			ZoomedCameraFOV,
			DeltaTime,
			ZoomInterpSpeed);
	} else
	{
		CurrentCameraFOV = FMath::FInterpTo(
			CurrentCameraFOV,
			DefaultCameraFOV,
			DeltaTime,
			ZoomInterpSpeed);
	}
	GetFollowCamera()->SetFieldOfView(CurrentCameraFOV);
}

void AShooterCharacter::CalculateCrosshairSpread(float DeltaTime)
{
	FVector2D WalkSpeedRange{ 0.0f, 600.0f };
	FVector2D VelocityMultiplierRange{ 0.0f, 1.0f };
	FVector Velocity{ GetVelocity() };
	Velocity.Z = 0.0f;

	CrosshairVelocityFactor = FMath::GetMappedRangeValueClamped(
		WalkSpeedRange,
		VelocityMultiplierRange,
		Velocity.Size());

	// Calculate crosshair in air factor
	if (GetCharacterMovement()->IsFalling())
	{
		CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor,
			2.25f,
			DeltaTime,
			2.25f);
	}
	else
	{
		CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor,
			0.0f,
			DeltaTime,
			30.0f);
	}

	// Calculate crosshair aim factor
	if (bAiming)
	{
		CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor,
			-0.6f,
			DeltaTime,
			2.25f);
	}
	else
	{
		CrosshairAimFactor = FMath::FInterpTo(CrosshairAimFactor,
			0.0f,
			DeltaTime,
			30.0f);
	}

	// Calculate crosshair shooting factor
	if (bIsFiringBullet)
	{
		CrosshairShootingFactor = FMath::FInterpTo(CrosshairShootingFactor,
				0.3f,
				DeltaTime,
				60.0f);
	}
	else
	{
		CrosshairShootingFactor = FMath::FInterpTo(CrosshairShootingFactor,
			0.0f,
			DeltaTime,
			60.0f);
	}
	
	
	CrosshairSpreadMultiplier = 0.5f + CrosshairVelocityFactor +
		CrosshairInAirFactor +
			CrosshairAimFactor +
				CrosshairShootingFactor;
}

// Called every frame
void AShooterCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	CameraInterpZoom(DeltaTime);
	CalculateCrosshairSpread(DeltaTime);
}

// Called to bind functionality to input
void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
    check(InputComponent);

	// Input bindings
	// Movement
	PlayerInputComponent->BindAxis("MoveForward", this, &AShooterCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AShooterCharacter::MoveRight);
	PlayerInputComponent->BindAxis("TurnRate", this, &AShooterCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AShooterCharacter::LookUpRate);

	// Jump
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AShooterCharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &AShooterCharacter::StopJumping);

	// Fire Weapon
	PlayerInputComponent->BindAction("FireButton", IE_Pressed, this, &AShooterCharacter::FireButtonPressed);
	PlayerInputComponent->BindAction("FireButton", IE_Released, this, &AShooterCharacter::FireButtonReleased);

	// Aim Weapon
	PlayerInputComponent->BindAction("AimButton", IE_Pressed, this, &AShooterCharacter::AimingButtonPressed);
	PlayerInputComponent->BindAction("AimButton", IE_Released, this, &AShooterCharacter::AimingButtonReleased);
}

float AShooterCharacter::GetCrosshairSpreadMultiplier() const
{
	return CrosshairSpreadMultiplier;
}

void AShooterCharacter::StartCrosshairBulletFire()
{
	bIsFiringBullet = true;
	
	GetWorldTimerManager().SetTimer(
		CrosshairShootTimer,
		this,
		&AShooterCharacter::FinishCrosshairBulletFire,
		ShootTimeDuration);
}

void AShooterCharacter::FinishCrosshairBulletFire()
{
	bIsFiringBullet = false;
}

