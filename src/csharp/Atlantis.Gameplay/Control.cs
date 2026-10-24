// Spec 0058 R1, Plan 0058 P6: RuntimeControl's value types (Spec 0055) --
// status, step requests and their frame reports, diagnostics. Plain values.
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay;

// Plan 0065 P4 (ADR-0131 D5): SceneGeneration counts the Runtime's scene reloads (0: none).
public sealed record RuntimeStatus(bool Paused, ulong Frame, AssetGuid Scene, ulong SceneGeneration = 0);

/// <summary>
/// Run <paramref name="Frames"/> frames (at least one); optionally capture the last to a PNG. Plan 0066 P2 (Spec 0066
/// R5): a capture shows no Viewport overlay unless <paramref name="WithOverlay"/>, and then only this client's own.
/// </summary>
public sealed record StepRequest(uint Frames, string? ImagePath = null, bool WithOverlay = false);

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
    NoGeometry,  // Spec 0064 / ADR-0128: a ray-cast on a Runtime keeping no pick geometry
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

// Spec 0064 / ADR-0128 D1-D3 (Plan 0064 P3): the camera the Runtime draws
// from, and the ray-cast against what it draws.

/// <summary>
/// A camera to draw from instead of the scene's: <c>WorldMatrix</c> holds the camera's 16-float world matrix,
/// column-major (the scene camera's convention: it looks down its local -Z). Lens values as the World's Camera.
/// </summary>
public sealed record ViewportCamera(IReadOnlyList<float> WorldMatrix, float FovYRadians, float NearZ, float FarZ);

/// <summary>A world-space ray; <c>Direction</c> is a unit vector.</summary>
public sealed record Ray(Vector3 Origin, Vector3 Direction);

/// <summary>The nearest Renderable a ray hits, and how far along the ray.</summary>
public sealed record RayHit(EntityGuid Entity, float Distance);

/// <summary>A ray-cast's outcome: a hit or a miss (<c>Hit</c> null), or why there is no answer.</summary>
public sealed record RaycastResult(RayHit? Hit, ControlError? Error)
{
    public bool IsOk => Error is null;
}
