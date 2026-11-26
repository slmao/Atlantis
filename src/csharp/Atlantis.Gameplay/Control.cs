// Spec 0058 R1, Plan 0058 P6: RuntimeControl's value types (Spec 0055) --
// status, step requests and their frame reports, diagnostics. Plain values.
using System.Collections.Generic;
using System.Numerics;

namespace Atlantis.Gameplay;

// Plan 0065 P4 (ADR-0131 D5): SceneGeneration counts the Runtime's scene reloads (0: none).
public sealed record RuntimeStatus(bool Paused, ulong Frame, AssetGuid Scene, ulong SceneGeneration = 0)
{
    public bool SupportsRenderLookCapture { get; init; }
}

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
    ulong DrawItemCount)
{
    /// <summary>Effective camera compensation used for this frame. Null on legacy reports.</summary>
    public float? ExposureCompensationEv { get; init; }
}

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

public enum RenderLookCaptureError
{
    Unsupported, Busy, InvalidRequest, StaleProjection, TargetChanged, Unavailable, BudgetExceeded, CaptureFailed
}

public sealed record RenderLookCaptureRequest(Authoring.RequireRenderLookDocument Guard, Authoring.ApplyId Apply,
    ulong Generation, ViewportCamera Camera, IReadOnlyList<Authoring.SetProperty> A,
    IReadOnlyList<Authoring.SetProperty> B, string OutputA, string OutputB);

public sealed record RenderLookCapturedPair(FrameReport A, FrameReport B,
    Authoring.DocumentToken Document, Authoring.ApplyId Apply, ulong Generation, EntityGuid ActiveCamera)
{
    public const uint Width = 960;
    public const uint Height = 540;
}

public sealed record RenderLookCaptureResult(RenderLookCapturedPair? Pair, RenderLookCaptureError? Error);

public enum ViewportBoundsKind { Mesh, Origin }

public sealed record ViewportBounds(EntityGuid Entity, ViewportBoundsKind Kind, Vector3 Min, Vector3 Max);

/// <summary>One Runtime scene sample. Bounds are in request order and the camera excludes any viewport override.</summary>
public sealed record ViewportNavigation(ulong SceneGeneration, ViewportCamera SceneCamera, IReadOnlyList<ViewportBounds> Bounds)
{
    public const int MaxTargets = 64;
}

public enum ViewportNavigationError
{
    InvalidRequest, SceneCameraUnavailable, TargetUnavailable, GeometryUnavailable, InvalidBounds, Unsupported, RpcFailure,
}

public sealed record ViewportNavigationFailure(ViewportNavigationError Code, EntityGuid? Target = null, string? Detail = null);

public sealed record ViewportNavigationResult(ViewportNavigation? Value, ViewportNavigationFailure? Error)
{
    public bool IsOk => Value is not null;
}

/// <summary>A world-space ray; <c>Direction</c> is a unit vector.</summary>
public sealed record Ray(Vector3 Origin, Vector3 Direction);

/// <summary>The nearest Renderable a ray hits, and how far along the ray.</summary>
public sealed record RayHit(EntityGuid Entity, float Distance);

public sealed record MaterialBindingPickRequest(Ray Ray, float MinDistance, float MaxDistance,
    ulong SceneGeneration, ViewportStamp? Viewport = null);
public sealed record MaterialBindingPick(ulong SceneGeneration, ViewportStamp? Viewport,
    EntityGuid Entity, AssetGuid Mesh, AssetGuid Material, float Distance)
{
    public const string Slot = "Renderable.materialAsset";
}
public enum MaterialBindingPickError
{
    InvalidRequest, Unavailable, Unsupported, StaleGeneration, StaleViewport, NotPresented,
    ActivePolicy, NoGeometry, Miss, Ambiguous, UnsupportedBinding, TooLarge, RpcFailure,
}
public sealed record MaterialBindingPickResult(MaterialBindingPick? Value, MaterialBindingPickError? Error);

public sealed record AlignmentFace(EntityGuid Entity, AssetGuid Mesh, uint FirstTriangle, uint SecondTriangle,
    float Distance, IReadOnlyList<Vector3> Corners, Vector3 Normal, IReadOnlyList<float> WorldMatrix)
{
    public bool SameEvidence(AlignmentFace other)
    {
        if (Entity != other.Entity || Mesh != other.Mesh || FirstTriangle != other.FirstTriangle ||
            SecondTriangle != other.SecondTriangle || Distance != other.Distance || Normal != other.Normal ||
            Corners.Count != other.Corners.Count || WorldMatrix.Count != other.WorldMatrix.Count) return false;
        for (int i = 0; i < Corners.Count; ++i)
            if (Corners[i] != other.Corners[i]) return false;
        for (int i = 0; i < WorldMatrix.Count; ++i)
            if (WorldMatrix[i] != other.WorldMatrix[i]) return false;
        return true;
    }
}
public sealed record AlignmentFaces(ulong SceneGeneration, IReadOnlyList<AlignmentFace> Faces);
public enum AlignmentFaceError { InvalidRequest, NoGeometry, Miss, UnsupportedFace, Ambiguous, TooLarge, Unsupported, RpcFailure }
public sealed record AlignmentFaceFailure(AlignmentFaceError Code, int? RayIndex = null, string? Detail = null);
public sealed record AlignmentFacesResult(AlignmentFaces? Value, AlignmentFaceFailure? Error);

/// <summary>A ray-cast's outcome: a hit or a miss (<c>Hit</c> null), or why there is no answer.</summary>
public sealed record RaycastResult(RayHit? Hit, ControlError? Error)
{
    public bool IsOk => Error is null;
}
