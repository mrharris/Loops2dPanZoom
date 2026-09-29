// Copyright 2026 Loops Creative Studio. All Rights Reserved.

#include "Loops2DPanZoomInputProcessor.h"
#include "Loops2DPanZoomSubsystem.h"
#include "EditorViewportClient.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "SEditorViewport.h"
#include "Loops2DPanZoomCommands.h"
#include "Loops2DPanZoomSettings.h"
#include "EditorModeManager.h"
#include "EditMode/ControlRigEditMode.h"

namespace Loops2DPanZoomInput
{
	static FEditorViewportClient* GetActiveEditorViewportClient()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		FViewport* ActiveVP = GEditor->GetActiveViewport();
		if (ActiveVP)
		{
			for (FEditorViewportClient* Candidate : GEditor->GetAllViewportClients())
			{
				if (Candidate && Candidate->Viewport == ActiveVP)
				{
					return Candidate;
				}
			}
		}
		// Only fall back to level viewports; an asset editor's viewport is never the intended target.
		for (FEditorViewportClient* Candidate : GEditor->GetAllViewportClients())
		{
			if (Candidate && Candidate->IsLevelEditorClient())
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	static FEditorViewportClient* GetHoveredViewportClient(FSlateApplication& SlateApp, const FVector2D& ScreenPos)
	{
		if (!GEditor)
		{
			return nullptr;
		}
		// The hit test only returns visible widgets, so viewports in hidden tabs are skipped.
		const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(ScreenPos, SlateApp.GetInteractiveTopLevelWindows());
		for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
		{
			const SWidget* HitWidget = &Path.Widgets[Index].Widget.Get();
			for (FEditorViewportClient* Candidate : GEditor->GetAllViewportClients())
			{
				const TSharedPtr<SEditorViewport> ViewportWidget = Candidate ? Candidate->GetEditorViewportWidget() : nullptr;
				if (ViewportWidget.Get() == HitWidget)
				{
					return Candidate;
				}
			}
		}
		return nullptr;
	}

	// Target the viewport under the cursor so a hotkey doesn't need a click to focus it first.
	// Falls back to the active viewport when the cursor isn't over one (e.g. over the Curve Editor).
	static FEditorViewportClient* GetTargetViewportClient(FSlateApplication& SlateApp)
	{
		FEditorViewportClient* Hovered = GetHoveredViewportClient(SlateApp, SlateApp.GetCursorPos());
		return Hovered ? Hovered : GetActiveEditorViewportClient();
	}

	static bool CanUseShortcuts(FSlateApplication& SlateApp)
	{
		if (!GEditor || !SlateApp.IsActive() || SlateApp.GetActiveModalWindow()) { return false; }
		// Protect text entry without requiring viewport focus.
		for (TSharedPtr<SWidget> Widget = SlateApp.GetKeyboardFocusedWidget(); Widget; Widget = Widget->GetParentWidget())
		{
			if (Widget->GetTypeAsString().Contains(TEXT("EditableText"))) { return false; }
		}
		return true;
	}

	// The above, plus the Animation Mode Only preference. Reserved for the shortcuts that must be
	// handed back to Unreal outside Animation Mode.
	static bool CanUseModeShortcuts(FSlateApplication& SlateApp)
	{
		return CanUseShortcuts(SlateApp)
			&& (!GetDefault<ULoops2DPanZoomSettings>()->bAnimationModeOnly
				|| GLevelEditorModeTools().IsModeActive(FControlRigEditMode::ModeName));
	}
}

void FLoops2DPanZoomInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// Key-up is lost on application switches; don't leave the modifier stuck.
	if (!SlateApp.IsActive())
	{
		bPanZoomKeyDown = false;
		bPanZoomKeyUsedWithMouse = false;
		PanZoomKeyViewportClient = nullptr;
		PanZoomHeldKey = EKeys::Invalid;
		bIsPanning = bIsZooming = false;
		CapturedViewportClient = nullptr;
	}
	// Cancel the gesture across mode/focus changes, but consume its releases.
	if (bPanZoomKeyDown && !Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp))
	{
		bPanZoomGestureCancelled = true;
	}

	if (GEditor)
	{
		if (ULoops2DPanZoomSubsystem* Subsystem = GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>())
		{
			Subsystem->TickAllAnimControlLocks();
		}
	}
}

bool FLoops2DPanZoomInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// Consume repeats only for a press we accepted; don't start mid-hold on a mode switch.
	if (bPanZoomKeyDown && Key == PanZoomHeldKey) { return true; }
	const FInputChord Chord(Key, InKeyEvent.IsShiftDown(), InKeyEvent.IsControlDown(), InKeyEvent.IsAltDown(), InKeyEvent.IsCommandDown());
	const auto& Commands = FLoops2DPanZoomCommands::Get();
	const bool bReset = Commands.Reset->HasActiveChord(Chord);
	if (bReset || Commands.ToggleAndDrag->HasActiveChord(Chord))
	{
		if (bPanZoomKeyDown || InKeyEvent.IsRepeat() || !Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp)) { return false; }
		ULoops2DPanZoomSubsystem* Subsystem = GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>();
		FEditorViewportClient* Client = Loops2DPanZoomInput::GetTargetViewportClient(SlateApp);
		if (!Subsystem || !Client) { return false; }
		bPanZoomKeyDown = true;
		PanZoomHeldKey = Key;
		PanZoomKeyViewportClient = Client;
		bPanZoomGestureCancelled = bReset;
		bPanZoomKeyUsedWithMouse = !SlateApp.GetPressedMouseButtons().IsEmpty();
		if (bReset) { Subsystem->Reset(Client); }
		return true;
	}

	if (Commands.ToggleControlLock->HasActiveChord(Chord))
	{
		// The Control Rig lock is a Control Rig feature, so it keeps the full gate, Animation Mode
		// included. Returning false leaves the key to Unreal instead of consuming it.
		if (!Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp))
		{
			return false;
		}

		ULoops2DPanZoomSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>() : nullptr;
		FEditorViewportClient* Client = Loops2DPanZoomInput::GetTargetViewportClient(SlateApp);
		if (!Subsystem || !Client)
		{
			return false;
		}

		Subsystem->ToggleAnimControlLock(Client);
		return true;
	}

	const bool bIsZoomIn = Commands.ZoomIn->HasActiveChord(Chord);
	const bool bIsZoomOut = Commands.ZoomOut->HasActiveChord(Chord);
	const bool bIsPanLeft = Commands.PanLeft->HasActiveChord(Chord);
	const bool bIsPanRight = Commands.PanRight->HasActiveChord(Chord);
	const bool bIsPanUp = Commands.PanUp->HasActiveChord(Chord);
	const bool bIsPanDown = Commands.PanDown->HasActiveChord(Chord);
	const bool bIsZoomToggle = Commands.ToggleZoom100->HasActiveChord(Chord);
	const bool bIsPanKey = bIsPanLeft || bIsPanRight || bIsPanUp || bIsPanDown;

	if (!bIsZoomIn && !bIsZoomOut && !bIsPanKey && !bIsZoomToggle)
	{
		return false;
	}

	// These stay available in every editor mode, as the Animation Mode Only preference promises, but
	// they must still never be taken out of a text field.
	if (!Loops2DPanZoomInput::CanUseShortcuts(SlateApp))
	{
		return false;
	}

	ULoops2DPanZoomSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>() : nullptr;
	FEditorViewportClient* Client = Loops2DPanZoomInput::GetTargetViewportClient(SlateApp);
	if (!Subsystem || !Client || !Subsystem->IsEnabled(Client))
	{
		return false;
	}

	if (bIsZoomToggle)
	{
		Subsystem->ToggleZoomTo100Percent(Client);
		return true;
	}

	if (bIsZoomIn || bIsZoomOut)
	{
		constexpr float KeyZoomStep = 0.12f;
		Subsystem->Zoom(Client, bIsZoomIn ? KeyZoomStep : -KeyZoomStep);
		return true;
	}

	if (Client->Viewport)
	{
		constexpr float KeyPanStepPixels = 20.0f;
		FVector2D ScreenDelta = FVector2D::ZeroVector;
		if (bIsPanLeft)
		{
			ScreenDelta = FVector2D(KeyPanStepPixels, 0.0f);
		}
		else if (bIsPanRight)
		{
			ScreenDelta = FVector2D(-KeyPanStepPixels, 0.0f);
		}
		else if (bIsPanUp)
		{
			ScreenDelta = FVector2D(0.0f, KeyPanStepPixels);
		}
		else if (bIsPanDown)
		{
			ScreenDelta = FVector2D(0.0f, -KeyPanStepPixels);
		}

		Subsystem->Pan(Client, ScreenDelta, Client->Viewport->GetSizeXY());
	}
	return true;
}

bool FLoops2DPanZoomInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() != PanZoomHeldKey || !bPanZoomKeyDown)
	{
		return false;
	}

	const bool bWasTap = !bPanZoomKeyUsedWithMouse && !bPanZoomGestureCancelled
		&& Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp);
	bPanZoomKeyDown = false;
	bPanZoomKeyUsedWithMouse = false;

	// Toggle the viewport targeted on key-down, even if the cursor has since left it, as long as it still exists.
	ULoops2DPanZoomSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>() : nullptr;
	FEditorViewportClient* Client = PanZoomKeyViewportClient;
	if (bWasTap && Subsystem && Client && GEditor->GetAllViewportClients().Contains(Client))
	{
		Subsystem->ToggleEnabled(Client);
	}
	PanZoomKeyViewportClient = nullptr;
	PanZoomHeldKey = EKeys::Invalid;
	return true;
}

bool FLoops2DPanZoomInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bPanZoomKeyDown || bPanZoomGestureCancelled || !Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp))
	{
		return false;
	}
	// Any click while the key is held means this was not a tap.
	bPanZoomKeyUsedWithMouse = true;

	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button != EKeys::MiddleMouseButton && Button != EKeys::RightMouseButton)
	{
		return false;
	}

	// Drag the viewport under the pointer; from any other panel (e.g. the Curve Editor),
	// drag the viewport targeted when the key went down.
	ULoops2DPanZoomSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>() : nullptr;
	FEditorViewportClient* Client = Loops2DPanZoomInput::GetHoveredViewportClient(SlateApp, MouseEvent.GetScreenSpacePosition());
	if (!Client && GEditor && GEditor->GetAllViewportClients().Contains(PanZoomKeyViewportClient))
	{
		Client = PanZoomKeyViewportClient;
	}
	if (!Subsystem || !Client)
	{
		return false;
	}

	// Dragging enables the mode, like Maya; no separate enable step needed.
	Subsystem->SetEnabled(Client, true);
	CapturedViewportClient = Client;
	LastScreenPos = MouseEvent.GetScreenSpacePosition();
	bIsPanning = (Button == EKeys::MiddleMouseButton);
	bIsZooming = (Button == EKeys::RightMouseButton);
	return true;
}

bool FLoops2DPanZoomInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (!bIsPanning && !bIsZooming)
	{
		return false;
	}

	const FKey Button = MouseEvent.GetEffectingButton();
	const bool bStoppingPan = bIsPanning && Button == EKeys::MiddleMouseButton;
	const bool bStoppingZoom = bIsZooming && Button == EKeys::RightMouseButton;

	if (bStoppingPan || bStoppingZoom)
	{
		bIsPanning = false;
		bIsZooming = false;
		CapturedViewportClient = nullptr;
		return true;
	}

	return false;
}

bool FLoops2DPanZoomInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if ((!bIsPanning && !bIsZooming) || !CapturedViewportClient || !GEditor)
	{
		return false;
	}

	ULoops2DPanZoomSubsystem* Subsystem = GEditor->GetEditorSubsystem<ULoops2DPanZoomSubsystem>();
	if (!Subsystem)
	{
		return false;
	}

	const FVector2D CurrentScreenPos = MouseEvent.GetScreenSpacePosition();
	const FVector2D Delta = CurrentScreenPos - LastScreenPos;
	LastScreenPos = CurrentScreenPos;

	// Releasing the key stops adjusting, but the drag stays consumed until
	// mouse-up so the viewport doesn't see moves for a press it never got.
	if (!bPanZoomKeyDown || bPanZoomGestureCancelled || !Loops2DPanZoomInput::CanUseModeShortcuts(SlateApp)
		|| !GEditor->GetAllViewportClients().Contains(CapturedViewportClient))
	{
		return true;
	}

	if (bIsPanning && CapturedViewportClient->Viewport)
	{
		Subsystem->Pan(CapturedViewportClient, Delta, CapturedViewportClient->Viewport->GetSizeXY());
	}
	else if (bIsZooming)
	{
		const float ZoomSpeed = 0.01f;
		Subsystem->Zoom(CapturedViewportClient, Delta.X * ZoomSpeed);
	}

	return true;
}
