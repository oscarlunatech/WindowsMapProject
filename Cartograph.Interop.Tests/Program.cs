using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Cartograph.Interop;

namespace Cartograph.InteropTests;

// A minimal assert harness. See the .csproj for why this is hand-rolled rather
// than xunit: no NuGet restore means no network dependency on a fresh clone.
internal static class Check
{
    private static int _passed;
    private static readonly List<string> Failures = [];
    private static string _section = "";

    public static void Section(string name)
    {
        _section = name;
        Console.WriteLine();
        Console.WriteLine($"--- {name}");
    }

    public static void That(bool condition, string what)
    {
        if (condition)
        {
            _passed++;
            Console.WriteLine($"  ok    {what}");
        }
        else
        {
            Failures.Add($"{_section}: {what}");
            Console.WriteLine($"  FAIL  {what}");
        }
    }

    public static void Equal<T>(T expected, T actual, string what) =>
        That(EqualityComparer<T>.Default.Equals(expected, actual),
             $"{what} (expected {expected}, got {actual})");

    public static void Near(double expected, double actual, double tolerance, string what) =>
        That(Math.Abs(expected - actual) <= tolerance,
             $"{what} (expected ~{expected}, got {actual})");

    // Asserts that `action` throws TException - and, just as importantly, that
    // it throws at all rather than taking the process down, which is what an
    // untranslated native exception would do.
    public static void Throws<TException>(Action action, string what) where TException : Exception
    {
        try
        {
            action();
            That(false, $"{what} (nothing was thrown)");
        }
        catch (TException)
        {
            That(true, what);
        }
        catch (Exception e)
        {
            That(false, $"{what} (threw {e.GetType().Name}: {e.Message})");
        }
    }

    public static int Report()
    {
        Console.WriteLine();
        if (Failures.Count == 0)
        {
            Console.WriteLine($"All {_passed} checks passed.");
            return 0;
        }

        Console.WriteLine($"{Failures.Count} FAILED, {_passed} passed:");
        foreach (string failure in Failures)
        {
            Console.WriteLine($"  - {failure}");
        }
        return 1;
    }
}

internal static class Program
{
    // Everything here opens the fixture in EPSG:4326 explicitly rather than
    // taking Core's EPSG:3857 default. That is the rule recorded in CLAUDE.md
    // after Phase 10: any test using degree coordinates - a bbox, an identify
    // probe point - has to pin 4326 or its numbers quietly mean something else.
    private const string Wgs84 = "EPSG:4326";

    private static string FixtureShapefile { get; set; } = "";

    private static int Main()
    {
        Console.OutputEncoding = Encoding.UTF8;

        string? root = FindRepositoryRoot();
        if (root is null)
        {
            Console.Error.WriteLine("Could not locate the repository root from " + AppContext.BaseDirectory);
            return 2;
        }
        FixtureShapefile = Path.Combine(root, "Cartograph.Core", "tests", "fixtures",
                                        "ne_110m_admin_0_countries.shp");
        Console.WriteLine($"fixture: {FixtureShapefile}");

        ModelIsExposed();
        StringsRoundTripAsUtf8();
        AttributesTakeTheirNaturalDotNetShape();
        GeometryIsWalkable();
        DisplayStateWritesThrough();
        IdentifyReachesCore();
        StylesheetBinds();
        RenderingProducesPixels();
        NativeExceptionsArriveManaged();
        ArgumentsAreValidated();
        ReprojectionKeepsHandlesValid();
        DisposalIsObservable();

        return Check.Report();
    }

    private static string? FindRepositoryRoot()
    {
        DirectoryInfo? dir = new(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "CLAUDE.md")))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        return null;
    }

    private static Map OpenFixture() => Map.Open([FixtureShapefile], Wgs84);

    private static void ModelIsExposed()
    {
        Check.Section("The map model crosses the boundary");

        using JobPool pool = new();
        Check.That(pool.ThreadCount >= 1, "JobPool reports at least one worker");

        using Map map = Map.Open([FixtureShapefile], Wgs84, pool);
        Check.Equal(1, map.LayerCount, "layer count");
        Check.Equal(177L, map.FeatureCount, "total feature count");
        Check.That(!map.IsEmpty, "map is not empty");
        Check.Equal(Wgs84, map.DisplayCrs, "display CRS is what we asked for");
        Check.Equal("EPSG:3857", Map.DefaultDisplayCrs, "Core's default display CRS is Web Mercator");

        MapLayer layer = map.GetLayer(0);
        Check.Equal("ne_110m_admin_0_countries", layer.Name, "layer name");
        Check.Equal(177, layer.FeatureCount, "layer feature count");
        Check.That(!layer.IsRaster, "the fixture is a vector layer");
        Check.That(layer.SourcePath.EndsWith(".shp", StringComparison.Ordinal), "source path");
        Check.That(layer.CrsWkt.Length > 0, "layer reports a CRS");

        // Degrees, because we pinned 4326. Antarctica reaches the south pole,
        // which is exactly the case Phase 10's area-of-use clamping exists for.
        Envelope extent = map.Extent;
        Check.That(extent.IsValid, "map extent is valid");
        Check.Near(-180.0, extent.MinX, 0.5, "extent min longitude");
        Check.Near(180.0, extent.MaxX, 0.5, "extent max longitude");
        Check.Near(-90.0, extent.MinY, 0.5, "extent min latitude");
        Check.Near(83.6, extent.MaxY, 1.0, "extent max latitude");
        Check.Near(360.0, extent.Width, 1.0, "extent width");
        Check.Near(0.0, extent.Center.X, 0.5, "extent centre longitude");
    }

    private static void StringsRoundTripAsUtf8()
    {
        Check.Section("Strings round-trip as UTF-8, not as the ANSI code page");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);

        // Feature 0 of this fixture is Fiji, and the Natural Earth attribute
        // table carries its name in fifteen scripts. If the boundary encoded
        // with the process code page instead of UTF-8, this would come back as
        // question marks.
        int japaneseName = FieldIndex(layer, "NAME_JA");
        Check.That(japaneseName >= 0, "the fixture has a NAME_JA field");

        object? value = layer.GetAttribute(0, japaneseName);
        Check.Equal("フィジー", value as string, "a Japanese attribute value survives the crossing");

        int greekName = FieldIndex(layer, "NAME_EL");
        Check.Equal("Φίτζι", layer.GetAttribute(0, greekName) as string,
                    "a Greek attribute value survives the crossing");

        // A path is the other direction: managed string in, UTF-8 out to GDAL.
        // The failure mode when this is wrong is a file that "does not exist".
        Check.That(layer.SourcePath.Contains("ne_110m_admin_0_countries", StringComparison.Ordinal),
                   "the path we passed in comes back intact");
    }

    private static void AttributesTakeTheirNaturalDotNetShape()
    {
        Check.Section("Attributes arrive as null / long / double / string");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);

        Check.That(layer.FieldCount > 10, "the fixture has a wide attribute table");

        int admin = FieldIndex(layer, "ADMIN");
        Check.Equal("Fiji", layer.GetAttribute(0, admin) as string, "a string field is a String");

        FieldDefinition adminField = layer.GetField(admin);
        Check.Equal("ADMIN", adminField.Name, "field name");
        Check.Equal(FieldType.String, adminField.Type, "field type");

        int labelRank = FieldIndex(layer, "LABELRANK");
        object? rank = layer.GetAttribute(0, labelRank);
        Check.That(rank is long or double, $"a numeric field is numeric (got {rank?.GetType().Name ?? "null"})");

        Check.That(layer.GetFeatureId(0) >= 0, "feature id is readable");
    }

    private static void GeometryIsWalkable()
    {
        Check.Section("Geometry is walkable part-by-ring-by-point");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);

        using Geometry geometry = layer.GetFeatureGeometry(0);
        Check.That(geometry.Type is GeometryType.Polygon or GeometryType.MultiPolygon,
                   $"Fiji is a polygon of some kind (got {geometry.Type})");
        Check.That(geometry.PartCount >= 1, "at least one part");

        int ringCount = geometry.RingCount(0);
        Check.That(ringCount >= 1, "at least one ring in the first part");

        MapPoint[] ring = geometry.Ring(0, 0);
        Check.That(ring.Length >= 4, "a closed ring has at least four points");

        Envelope geometryExtent = geometry.Extent;
        Check.That(geometryExtent.IsValid, "geometry reports a valid extent");
        Check.That(geometryExtent.MinY is > -30 and < 0, "Fiji is in the southern hemisphere");

        // Ring indices are bounds-checked on this side, because Core's
        // operator[] would not check them.
        Check.Throws<ArgumentOutOfRangeException>(() => geometry.Ring(0, ringCount),
                                                  "a ring index past the end throws");
        Check.Throws<ArgumentOutOfRangeException>(() => geometry.Ring(-1, 0),
                                                  "a negative part index throws");
    }

    private static void DisplayStateWritesThrough()
    {
        Check.Section("Visibility and opacity write through to the native layer");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);

        Check.That(layer.Visible, "layers start visible");
        Check.Equal(1.0f, layer.Opacity, "layers start opaque");

        layer.Visible = false;
        // A *different* handle to the same layer, to prove the write reached
        // the native map rather than a field on the wrapper.
        Check.That(!map.GetLayer(0).Visible, "hiding a layer is visible through another handle");
        layer.Visible = true;

        layer.Opacity = 0.25f;
        Check.Equal(0.25f, map.GetLayer(0).Opacity, "opacity is visible through another handle");

        layer.Opacity = 2.0f;
        Check.Equal(1.0f, layer.Opacity, "Core clamps opacity above 1");
        layer.Opacity = -1.0f;
        Check.Equal(0.0f, layer.Opacity, "Core clamps opacity below 0");
        layer.Opacity = 1.0f;
    }

    private static void IdentifyReachesCore()
    {
        Check.Section("Identify");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);
        int admin = FieldIndex(layer, "ADMIN");

        // Well inside Brazil, in degrees.
        IdentifyHit[] hits = Query.Identify(map, new MapPoint(-55.0, -10.0), 0.0);
        Check.Equal(1, hits.Length, "one country contains the probe point");
        if (hits.Length > 0)
        {
            Check.Equal(0, hits[0].LayerIndex, "hit names the only layer");
            Check.Equal(0.0, hits[0].Distance, "a point inside a polygon is at distance zero");
            Check.That(!hits[0].IsRaster, "a vector hit is not a raster hit");
            Check.Equal("Brazil", layer.GetAttribute(hits[0].FeatureIndex, admin) as string,
                        "the hit is Brazil");
        }

        // The Gulf of Guinea: 0,0 is open ocean in every direction.
        Check.Equal(0, Query.Identify(map, new MapPoint(0.0, 0.0), 0.0).Length,
                    "a point in the ocean hits nothing");

        // Clicking what you cannot see must not report a hit - Core's rule,
        // checked here because it is easy to lose across a boundary.
        layer.Visible = false;
        Check.Equal(0, Query.Identify(map, new MapPoint(-55.0, -10.0), 0.0).Length,
                    "a hidden layer is skipped by identify");
        layer.Visible = true;
    }

    private static void StylesheetBinds()
    {
        Check.Section("Stylesheet");

        using Map map = OpenFixture();

        using Stylesheet defaults = Stylesheet.Defaults(map);
        Check.Equal(1, defaults.LayerCount, "built against one layer");
        Check.Equal(1, defaults.SymbolCount, "one default symbol, deduplicated");
        Check.Equal(0, defaults.SymbolIndex(0, 0), "every feature resolves to it");

        // These are the values renderer.cpp hardcoded before Phase 7, and what
        // keeps the golden-image test byte-exact. If they ever change, that
        // test needs re-baselining - so the boundary pins them too.
        Symbol symbol = defaults.GetSymbol(0);
        Check.Near(0.85, symbol.Fill.R, 1e-6, "default fill is the familiar grey");
        Check.Near(1.0, symbol.Fill.A, 1e-6, "default fill is opaque");
        Check.Near(1.0, symbol.PolygonStrokeWidth, 1e-6, "default polygon stroke width");
        Check.Near(3.0, symbol.PointRadius, 1e-6, "default point radius");

        const string json = """
            {
              "layers": {
                "ne_110m_admin_0_countries": {
                  "type": "categorized",
                  "field": "CONTINENT",
                  "categories": [
                    { "value": "Africa", "symbol": { "fill": "#e8c39e" } },
                    { "value": "Europe", "symbol": { "fill": "#a5b8d9" } }
                  ],
                  "fallback": { "fill": "#dddddd" }
                }
              }
            }
            """;
        using Stylesheet categorized = Stylesheet.FromJson(json, map);
        Check.Equal(3, categorized.SymbolCount, "two categories plus a fallback");

        // Core reports an unknown layer or field rather than falling back
        // silently, because a typo would otherwise look like the style file
        // being ignored. That has to survive the crossing as a typed exception.
        Check.Throws<StyleException>(
            () => Stylesheet.FromJson("""{"layers":{"no_such_layer":{"symbol":{}}}}""", map),
            "an unknown layer name throws StyleException");
        Check.Throws<StyleException>(
            () => Stylesheet.FromJson(
                """{"layers":{"ne_110m_admin_0_countries":{"type":"categorized","field":"NO_SUCH_FIELD","categories":[]}}}""",
                map),
            "an unknown field name throws StyleException");
        Check.Throws<StyleException>(() => Stylesheet.FromJson("not json at all", map),
                                     "malformed JSON throws StyleException");
    }

    private static void RenderingProducesPixels()
    {
        Check.Section("Rendering");

        using Map map = OpenFixture();
        using Stylesheet stylesheet = Stylesheet.Defaults(map);

        // 2:1, matching the world's 360x180 - so the viewport does not have to
        // grow the extent to fill the frame and the numbers stay exact.
        using Viewport viewport = new(new Envelope(-180, -90, 180, 90, true), new ScreenSize(256, 128));
        Check.Equal(256, viewport.Size.Width, "viewport keeps its width");
        Check.Near(256.0 / 360.0, viewport.Scale, 1e-9, "scale is pixels per degree");

        MapPoint centre = viewport.MapToScreen(new MapPoint(0, 0));
        Check.Near(128.0, centre.X, 0.5, "the prime meridian lands mid-frame");
        Check.Near(64.0, centre.Y, 0.5, "the equator lands mid-frame");

        MapPoint back = viewport.ScreenToMap(centre);
        Check.Near(0.0, back.X, 1e-6, "screen-to-map round-trips in X");
        Check.Near(0.0, back.Y, 1e-6, "screen-to-map round-trips in Y");

        // A map with no raster layers makes this a no-op, but it must still be
        // callable - the shell will not know which kind it has.
        Renderer.RefreshRasterLayers(map, viewport, stylesheet);

        RenderedImage image = Renderer.Render(map, viewport, stylesheet);
        Check.Equal(256, image.Width, "rendered width");
        Check.Equal(128, image.Height, "rendered height");
        Check.Equal(1024, image.Stride, "stride is width * 4");
        Check.Equal(256 * 128 * 4, image.Pixels.Length, "pixel buffer length");

        // Premultiplied BGRA: the default land fill is 0.85 grey, which is 217
        // out of 255. Rather than probe one coordinate and hope - at 256x128
        // the whole world is small enough that a single sample lands on a
        // black country outline about as often as on a fill - count how many
        // pixels came out exactly that colour. This is the same technique
        // test_render.cpp uses for the culled path, and for the same reason.
        int landPixels = 0;
        int opaquePixels = 0;
        for (int i = 0; i < image.Pixels.Length; i += 4)
        {
            byte blue = image.Pixels[i];
            byte green = image.Pixels[i + 1];
            byte red = image.Pixels[i + 2];
            byte alpha = image.Pixels[i + 3];
            if (alpha == 255)
            {
                opaquePixels++;
            }
            if (red == 217 && green == 217 && blue == 217 && alpha == 255)
            {
                landPixels++;
            }
        }
        Check.Equal(256 * 128, opaquePixels, "every pixel is opaque");
        Check.That(landPixels > 1000,
                   $"a good share of the frame is the 0.85 default grey (got {landPixels} pixels)");

        string png = Path.Combine(Path.GetTempPath(), "cartograph-interop-test.png");
        if (File.Exists(png))
        {
            File.Delete(png);
        }
        Renderer.RenderToPng(map, viewport, stylesheet, png);
        Check.That(File.Exists(png), "RenderToPng wrote a file");
        Check.That(new FileInfo(png).Length > 0, "the PNG is not empty");
        File.Delete(png);
    }

    private static void NativeExceptionsArriveManaged()
    {
        Check.Section("Core's exceptions arrive as typed managed exceptions");

        string missing = Path.Combine(Path.GetTempPath(), "no-such-file.shp");

        // The single most important property of this assembly: a native
        // exception that escaped untranslated would not be catchable here, it
        // would take the process down. What matters is that it arrives as a
        // typed managed exception at all.
        //
        // Which type it is, is Core's business rather than the boundary's, and
        // for a missing file Core currently answers RasterException - Map::open
        // tries vector then raster and lets the raster failure escape, though
        // both map.h's docstring and the comment above addPath in map.cpp say
        // the vector error is the one reported. Asserted here as the base type
        // so this test pins the boundary's guarantee and not Core's open
        // (see the Phase 12 DECISIONS entry, which records the discrepancy).
        Check.Throws<CartographException>(() => Map.Open([missing], Wgs84),
                                          "opening a missing file throws a Cartograph exception");

        Check.Throws<CartographException>(
            () => Map.Open([FixtureShapefile], "EPSG:not-a-real-code"),
            "an unparseable CRS throws a Cartograph exception");

        // And the hierarchy is usable: catching the base type catches the
        // specific ones, which is what lets a shell have one handler for
        // "Cartograph could not do that" and specific ones where it cares.
        try
        {
            Map.Open([missing], Wgs84);
            Check.That(false, "a specific exception is catchable as CartographException");
        }
        catch (CartographException e)
        {
            Check.That(e.GetType() != typeof(CartographException),
                       $"a specific exception is catchable as CartographException (got {e.GetType().Name})");
            Check.That(e.Message.Length > 0, "the native message survives translation");
        }

        Check.Throws<StyleException>(
            () => Stylesheet.FromFile(Path.Combine(Path.GetTempPath(), "no-such-style.json"),
                                      OpenFixture()),
            "a missing style file throws StyleException");
    }

    private static void ArgumentsAreValidated()
    {
        Check.Section("Bad arguments throw instead of corrupting memory");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);

        Check.Throws<ArgumentOutOfRangeException>(() => map.GetLayer(-1), "a negative layer index throws");
        Check.Throws<ArgumentOutOfRangeException>(() => map.GetLayer(map.LayerCount),
                                                  "a layer index past the end throws");
        Check.Throws<ArgumentOutOfRangeException>(() => layer.GetAttribute(0, layer.FieldCount),
                                                  "a field index past the end throws");
        Check.Throws<ArgumentOutOfRangeException>(() => layer.GetFeatureGeometry(layer.FeatureCount),
                                                  "a feature index past the end throws");
        Check.Throws<ArgumentNullException>(() => Map.Open(null!, Wgs84), "a null path array throws");
        Check.Throws<ArgumentNullException>(() => Query.Identify(null!, new MapPoint(0, 0), 0),
                                            "identifying a null map throws");

        // Core says "calling a raster accessor on a vector layer is a
        // programming error, check isRaster() first" in a comment. A managed
        // caller cannot be trusted with undefined behaviour, so here it is an
        // exception.
        Check.Throws<InvalidOperationException>(() => _ = layer.BandCount,
                                                "asking a vector layer for its band count throws");
        Check.Throws<InvalidOperationException>(() => layer.SampleBands(new MapPoint(0, 0)),
                                                "sampling bands on a vector layer throws");
    }

    private static void ReprojectionKeepsHandlesValid()
    {
        Check.Section("SetDisplayCrs re-reads the sources and keeps handles valid");

        using Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);
        layer.Visible = false;
        layer.Opacity = 0.5f;

        Check.Near(180.0, map.Extent.MaxX, 0.5, "extent is in degrees before");

        map.SetDisplayCrs("EPSG:3857");

        Check.Equal("EPSG:3857", map.DisplayCrs, "display CRS changed");
        Check.Near(20037508.34, map.Extent.MaxX, 1.0, "extent is in metres after");
        Check.Equal(1, map.LayerCount, "layer count is unchanged");

        // The handle taken before the reprojection still resolves, because it
        // holds an index rather than a pointer into a vector that has since
        // been replaced wholesale.
        Check.That(!layer.Visible, "visibility survived the reprojection");
        Check.Equal(0.5f, layer.Opacity, "opacity survived the reprojection");
        Check.Equal("ne_110m_admin_0_countries", layer.Name, "the stale handle still resolves");

        Check.Throws<CartographException>(() => map.SetDisplayCrs("EPSG:not-a-real-code"),
                                          "a bad CRS throws");
        Check.Equal("EPSG:3857", map.DisplayCrs,
                    "and leaves the map untouched (Core is strongly exception-safe here)");
    }

    private static void DisposalIsObservable()
    {
        Check.Section("Disposal");

        Map map = OpenFixture();
        MapLayer layer = map.GetLayer(0);
        map.Dispose();

        Check.Throws<ObjectDisposedException>(() => _ = map.LayerCount,
                                              "using a disposed Map throws");
        Check.Throws<ObjectDisposedException>(() => _ = layer.Name,
                                              "a handle into a disposed Map throws");

        // Dispose is idempotent, as .NET requires.
        map.Dispose();
        Check.That(true, "disposing twice is harmless");
    }

    private static int FieldIndex(MapLayer layer, string name)
    {
        for (int i = 0; i < layer.FieldCount; i++)
        {
            if (layer.GetField(i).Name == name)
            {
                return i;
            }
        }
        return -1;
    }
}
