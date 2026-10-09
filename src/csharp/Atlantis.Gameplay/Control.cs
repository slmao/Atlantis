// Spec 0058 R1, Plan 0058 P6: RuntimeControl's value types (Spec 0055) --
// status, step requests and their frame reports, diagnostics. Plain values.
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay;

public sealed record RuntimeStatus(bool Paused, ulong Frame, AssetGuid Scene);

/// <summary>Run <paramref name="Frames"/> frames (at least one); optionally capture the last to a PNG.</summary>
public sealed record StepRequest(uint Frames, string? ImagePath = null);

public sealed record FrameDirectionalLight(Vector3 Direction, Vector3 Color, float Intensity);

public sealed record FramePointLight(Vector3 Position, Vector3 Color, float Intensity, float Range);

/// <summary>What the renderer drew in a step's last frame. <c>View</c> and <c>Projection</c> hold 16 floats each.</summary>
public sealed record FrameData(
    IReadOnlyList<FrameDirectionalLight> DirectionalLights,
    IReadOnlyList<FramePointLight> PointLights,
    IReadOnlyList<float> View,
    IReadOnlyList<float> Projection,
    ulong DrawItemCount);

public sealed record CapturedImage(string Path, uint Width, uint Height);

public sealed record FrameReport(ulong Frame, bool Applied, FrameData Data, CapturedImage? Image);

/// <summary>Why a step has no report (the wire names).</summary>
public enum ControlError
{
    InvalidRequest,
    NotRendering,
    CaptureFailed,
    Stopped,
}

/// <summary>A step's outcome: a report, or why there is none.</summary>
public sealed record StepResult(FrameReport? Report, ControlError? Error)
{
    public bool IsOk => Report is not null;
}

public enum DiagnosticSeverity
{
    Warning,
    Error,
    Fatal,
}

public sealed record Diagnostic(ulong Sequence, DiagnosticSeverity Severity, string Message);

public sealed record DiagnosticBatch(IReadOnlyList<Diagnostic> Entries, ulong Latest, ulong Dropped);
