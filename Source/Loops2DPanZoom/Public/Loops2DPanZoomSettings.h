// Copyright 2026 Loops Creative Studio. All Rights Reserved.
#pragma once
#include "Engine/DeveloperSettings.h"
#include "Loops2DPanZoomSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, meta=(DisplayName="Loops 2D Pan/Zoom"))
class LOOPS2DPANZOOM_API ULoops2DPanZoomSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	// Outside Animation Mode, leave the toggle/drag and reset keys to Unreal.
	// This is so our \ hotkey won't conflict with the place Colour Calibrator tool.
	// The toolbar and existing numpad shortcuts remain available in all modes.
	UPROPERTY(Config, EditAnywhere, Category="Shortcuts")
	bool bAnimationModeOnly = true;
	// While Pan/Zoom is on, fill a camera's Constrain Aspect Ratio bars with the view beyond the frame.
	// The frame itself stays where the bars put it; this only matters when zooming makes the extra space useful.
	UPROPERTY(Config, EditAnywhere, Category="Display")
	bool bHideCameraLetterbox = true;
};
