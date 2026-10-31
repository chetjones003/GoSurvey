// Loaded by the Civil 3D runtime (NETLOAD) when GoSurvey requests native Civil 3D data.
// This bridge deliberately runs inside Civil 3D: AECC database objects are not AutoCAD entities.
using System;
using System.Collections;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Collections.Generic;
using System.Linq;
using Autodesk.AutoCAD.ApplicationServices;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.EditorInput;
using Autodesk.AutoCAD.Geometry;
using Autodesk.AutoCAD.Runtime;
using Autodesk.Civil;
using Autodesk.Civil.ApplicationServices;
using Autodesk.Civil.DatabaseServices;
using Autodesk.Civil.DatabaseServices.Styles;

public sealed class GoSurveyCivilBridge : IExtensionApplication
{
    public void Initialize() { }
    public void Terminate() { }

#if !GOSURVEY_IMPORT_ONLY
    [CommandMethod("GOSURVEY_CIVIL_EXPORT", CommandFlags.Session)]
    public void ExportCivilObjects()
    {
        Document document = Application.DocumentManager.MdiActiveDocument;
        if (document == null)
            throw new InvalidOperationException("No active Civil 3D document.");

        PromptResult prompt = document.Editor.GetString("\nGoSurvey Civil data JSON path: ");
        if (prompt.Status != PromptStatus.OK || String.IsNullOrWhiteSpace(prompt.StringResult))
            return;

        CivilDocument civil = CivilDocument.GetCivilDocument(document.Database);
        StringBuilder json = new StringBuilder(1024 * 1024);
        json.Append("{\"schema\":1,\"points\":[");
        bool first = true;
        using (Transaction transaction = document.Database.TransactionManager.StartTransaction())
        {
            ObjectIdCollection allPointIds = civil.GetAllPointIds();
            foreach (ObjectId id in allPointIds)
            {
                CogoPoint point = transaction.GetObject(id, OpenMode.ForRead) as CogoPoint;
                if (point == null) continue;
                if (!first) json.Append(',');
                first = false;
                json.Append("{\"number\":").Append(point.PointNumber.ToString(CultureInfo.InvariantCulture));
                Point3d location = point.Location;
                AddNumber(json, "easting", location.X);
                AddNumber(json, "northing", location.Y);
                AddNumber(json, "elevation", location.Z);
                AddString(json, "rawDescription", point.RawDescription ?? String.Empty);
                string pointStyle = StyleName(transaction, point.StyleId);
                string labelStyle = StyleName(transaction, point.LabelStyleId);
                // In Civil 3D, points commonly inherit their visible style from the primary
                // point group. The per-point style IDs are then null even though the points have
                // a concrete effective marker and label style in the drawing.
                if ((String.IsNullOrEmpty(pointStyle) || String.IsNullOrEmpty(labelStyle)) &&
                    !point.PrimaryPointGroupId.IsNull && point.PrimaryPointGroupId.IsValid)
                {
                    PointGroup primaryGroup = transaction.GetObject(point.PrimaryPointGroupId, OpenMode.ForRead, false) as PointGroup;
                    if (primaryGroup != null)
                    {
                        if (String.IsNullOrEmpty(pointStyle))
                            pointStyle = StyleName(transaction, primaryGroup.PointStyleId);
                        if (String.IsNullOrEmpty(labelStyle))
                            labelStyle = StyleName(transaction, primaryGroup.PointLabelStyleId);
                    }
                }
                AddString(json, "pointStyle", pointStyle);
                AddString(json, "labelStyle", labelStyle);
                AddString(json, "pointStyleOverride", StyleName(transaction, point.StyleIdOverride));
                AddString(json, "labelStyleOverride", StyleName(transaction, point.LabelStyleIdOverride));
                json.Append('}');
            }

            json.Append("],\"pointStyles\":[");
            first = true;
            foreach (ObjectId id in civil.Styles.PointStyles)
            {
                PointStyle style = transaction.GetObject(id, OpenMode.ForRead) as PointStyle;
                if (style == null) continue;
                if (!first) json.Append(',');
                first = false;
                json.Append('{');
                AddString(json, "name", style.Name, false);
                json.Append('}');
            }

            json.Append("],\"pointLabelStyles\":[");
            first = true;
            foreach (ObjectId id in civil.Styles.LabelStyles.PointLabelStyles.LabelStyles)
            {
                LabelStyle style = transaction.GetObject(id, OpenMode.ForRead) as LabelStyle;
                if (style == null) continue;
                if (!first) json.Append(',');
                first = false;
                json.Append('{');
                AddString(json, "name", style.Name, false);
                json.Append('}');
            }

            json.Append("],\"surfaces\":[");
            first = true;
            foreach (ObjectId id in civil.GetSurfaceIds())
            {
                Autodesk.Civil.DatabaseServices.Surface surface = transaction.GetObject(id, OpenMode.ForRead) as Autodesk.Civil.DatabaseServices.Surface;
                if (surface == null) continue;
                if (!first) json.Append(',');
                first = false;
                json.Append('{');
                AddString(json, "name", surface.Name, false);
                AddString(json, "type", surface.GetType().Name);
                AddString(json, "style", surface.StyleName);
                json.Append(",\"triangles\":[");
                TinSurface tin = surface as TinSurface;
                bool firstTriangle = true;
                if (tin != null)
                {
                    foreach (TinSurfaceTriangle triangle in tin.Triangles)
                    {
                        if (!firstTriangle) json.Append(',');
                        firstTriangle = false;
                        json.Append('[');
                        AddPoint(json, triangle.Vertex1.Location);
                        json.Append(',');
                        AddPoint(json, triangle.Vertex2.Location);
                        json.Append(',');
                        AddPoint(json, triangle.Vertex3.Location);
                        json.Append(']');
                    }
                }
                json.Append("]}");
            }

            json.Append("],\"alignments\":[");
            first = true;
            foreach (ObjectId id in civil.GetAlignmentIds())
            {
                Alignment alignment = transaction.GetObject(id, OpenMode.ForRead) as Alignment;
                if (alignment == null) continue;
                if (!first) json.Append(',');
                first = false;
                json.Append('{');
                AddString(json, "name", alignment.Name, false);
                AddNumber(json, "startStation", alignment.StartingStation);
                AddNumber(json, "endStation", alignment.EndingStation);
                json.Append(",\"vertices\":[");
                double startStation = alignment.StartingStation;
                double endStation = alignment.EndingStation;
                int intervals = Math.Max(1, Math.Min(10000, (int)Math.Ceiling(Math.Abs(endStation - startStation))));
                for (int vertex = 0; vertex <= intervals; ++vertex)
                {
                    double station = startStation + (endStation - startStation) * vertex / intervals;
                    double easting = 0.0, northing = 0.0;
                    alignment.PointLocation(station, 0.0, ref easting, ref northing);
                    if (vertex != 0) json.Append(',');
                    json.Append('[').Append(Number(easting)).Append(',').Append(Number(northing)).Append(",0,0]");
                }
                json.Append("]}");
            }
            transaction.Commit();
        }

        json.Append("]}");
        File.WriteAllText(prompt.StringResult, json.ToString(), new UTF8Encoding(false));
        document.Editor.WriteMessage("\nGoSurvey exported Civil 3D data: {0}", prompt.StringResult);
    }
#endif

#if !GOSURVEY_EXPORT_ONLY
    [CommandMethod("GOSURVEY_CIVIL_IMPORT")]
    public void ImportCivilObjects()
    {
        Document document = Application.DocumentManager.MdiActiveDocument;
        if (document == null)
            throw new InvalidOperationException("No active Civil 3D document.");
        PromptResult prompt = document.Editor.GetString("\nGoSurvey model JSON path: ");
        if (prompt.Status != PromptStatus.OK || String.IsNullOrWhiteSpace(prompt.StringResult))
            return;

        string resultPath = prompt.StringResult + ".result";
        try
        {
            using (JsonDocument json = JsonDocument.Parse(File.ReadAllText(prompt.StringResult)))
            {
            CivilDocument civil = CivilDocument.GetCivilDocument(document.Database);
            JsonElement root = json.RootElement;
            int clonedAutoCadStyles = 0;
            int clonedCivilStyles = 0;
            int pointCount = -1;
            string sourceDrawing = ReadString(root, "sourceDwg");
            if (!String.IsNullOrWhiteSpace(sourceDrawing) && File.Exists(sourceDrawing) &&
                !String.Equals(Path.GetFullPath(sourceDrawing), Path.GetFullPath(document.Database.Filename), StringComparison.OrdinalIgnoreCase))
            {
                using (Database sourceDatabase = new Database(false, true))
                {
                    sourceDatabase.ReadDwgFile(sourceDrawing, FileOpenMode.OpenForReadAndAllShare, true, null);
                    clonedAutoCadStyles = CloneAutoCadStyleRecords(sourceDatabase, document.Database);
                    CivilDocument sourceCivil = CivilDocument.GetCivilDocument(sourceDatabase);
                    clonedCivilStyles = CloneCivilStyleCollections(sourceCivil.Styles, civil.Styles,
                        sourceDatabase, document.Database);
                    // Recreate Civil 3D objects through Civil APIs so points remain valid in
                    // the saved target drawing.
                    pointCount = ImportPoints(root, civil, document.Database);
                    sourceDatabase.CloseInput(true);
                }
            }
            if (pointCount < 0) pointCount = ImportPoints(root, civil, document.Database);
            int surfaceCount = ImportSurfaces(root, civil, document.Database);
            int alignmentCount = ImportAlignments(root, civil, document.Database);
            File.WriteAllText(resultPath, "{\"ok\":true,\"points\":" + pointCount +
                ",\"surfaces\":" + surfaceCount + ",\"alignments\":" + alignmentCount +
                ",\"autocadStyleGroups\":" + clonedAutoCadStyles + ",\"civilStyleGroups\":" + clonedCivilStyles +
                "}", new UTF8Encoding(false));
            document.Editor.WriteMessage("\nGoSurvey Civil 3D import complete: {0} points, {1} surfaces, {2} alignments; cloned {3} AutoCAD style groups and {4} Civil 3D style groups.",
                pointCount, surfaceCount, alignmentCount, clonedAutoCadStyles, clonedCivilStyles);
            }
        }
        catch (System.Exception exception)
        {
            File.WriteAllText(resultPath, JsonSerializer.Serialize(new { ok = false, error = exception.ToString() }),
                new UTF8Encoding(false));
            document.Editor.WriteMessage("\nGoSurvey Civil 3D import failed: {0}", exception.Message);
        }
    }

    private static int CloneAutoCadStyleRecords(Database source, Database target)
    {
        int groups = 0;
        groups += CloneSymbolTable(source, target, source.DimStyleTableId, target.DimStyleTableId);
        groups += CloneSymbolTable(source, target, source.TextStyleTableId, target.TextStyleTableId);
        groups += CloneStyleDictionary(source, target, "ACAD_MLEADERSTYLE");
        groups += CloneStyleDictionary(source, target, "ACAD_TABLESTYLE");
        return groups;
    }

    private static int CloneSymbolTable(Database source, Database target, ObjectId sourceId, ObjectId targetId)
    {
        ObjectIdCollection ids = new ObjectIdCollection();
        using (Transaction transaction = source.TransactionManager.StartOpenCloseTransaction())
        {
            SymbolTable table = transaction.GetObject(sourceId, OpenMode.ForRead) as SymbolTable;
            if (table == null) return 0;
            foreach (ObjectId id in table)
            {
                SymbolTableRecord record = transaction.GetObject(id, OpenMode.ForRead) as SymbolTableRecord;
                if (record != null && !String.IsNullOrWhiteSpace(record.Name) && !record.Name.StartsWith("*", StringComparison.Ordinal))
                    ids.Add(id);
            }
            transaction.Commit();
        }
        if (ids.Count == 0) return 0;
        source.WblockCloneObjects(ids, targetId, new IdMapping(), DuplicateRecordCloning.Replace, false);
        return 1;
    }

    private static int CloneStyleDictionary(Database source, Database target, string dictionaryName)
    {
        ObjectId sourceOwner = ObjectId.Null;
        ObjectId targetOwner = ObjectId.Null;
        ObjectIdCollection ids = new ObjectIdCollection();
        using (Transaction transaction = source.TransactionManager.StartOpenCloseTransaction())
        {
            DBDictionary namedObjects = transaction.GetObject(source.NamedObjectsDictionaryId, OpenMode.ForRead) as DBDictionary;
            if (namedObjects == null || !namedObjects.Contains(dictionaryName)) return 0;
            DBDictionary dictionary = transaction.GetObject(namedObjects.GetAt(dictionaryName), OpenMode.ForRead) as DBDictionary;
            if (dictionary == null) return 0;
            sourceOwner = dictionary.ObjectId;
            foreach (DBDictionaryEntry entry in dictionary)
                ids.Add(entry.Value);
            transaction.Commit();
        }
        if (ids.Count == 0) return 0;
        using (Transaction transaction = target.TransactionManager.StartTransaction())
        {
            DBDictionary namedObjects = transaction.GetObject(target.NamedObjectsDictionaryId, OpenMode.ForRead) as DBDictionary;
            if (namedObjects == null) return 0;
            if (!namedObjects.Contains(dictionaryName))
            {
                namedObjects.UpgradeOpen();
                DBDictionary created = new DBDictionary();
                targetOwner = namedObjects.SetAt(dictionaryName, created);
                transaction.AddNewlyCreatedDBObject(created, true);
            }
            else
            {
                targetOwner = namedObjects.GetAt(dictionaryName);
            }
            transaction.Commit();
        }
        if (sourceOwner.IsNull || targetOwner.IsNull) return 0;
        source.WblockCloneObjects(ids, targetOwner, new IdMapping(), DuplicateRecordCloning.Replace, false);
        return 1;
    }

    private static int CloneCivilStyleCollections(object sourceStyles, object targetStyles,
        Database sourceDatabase, Database targetDatabase)
    {
        HashSet<string> copiedOwners = new HashSet<string>(StringComparer.Ordinal);
        return CloneCivilStyleTree(sourceStyles, targetStyles, sourceDatabase, targetDatabase, copiedOwners, 0);
    }

    private static int CloneCivilStyleTree(object source, object target, Database sourceDatabase,
        Database targetDatabase, HashSet<string> copiedOwners, int depth)
    {
        if (source == null || target == null || depth > 4) return 0;
        int groups = 0;
        foreach (PropertyInfo property in source.GetType().GetProperties(BindingFlags.Instance | BindingFlags.Public))
        {
            if (!property.CanRead || !(property.Name.EndsWith("Styles", StringComparison.Ordinal) ||
                                       property.Name.EndsWith("LabelStyles", StringComparison.Ordinal))) continue;
            PropertyInfo targetProperty = target.GetType().GetProperty(property.Name, BindingFlags.Instance | BindingFlags.Public);
            if (targetProperty == null || !targetProperty.CanRead) continue;
            object sourceValue, targetValue;
            try { sourceValue = property.GetValue(source); targetValue = targetProperty.GetValue(target); }
            catch { continue; }
            if (sourceValue == null || targetValue == null) continue;

            ObjectIdCollection sourceIds = CollectObjectIds(sourceValue);
            if (sourceIds.Count > 0)
            {
                string key = source.GetType().FullName + ":" + property.Name;
                if (copiedOwners.Add(key))
                {
                    // Civil styles contain AECC-owned dictionaries and dependencies. Autodesk's
                    // style exporter understands those owners; AutoCAD WblockCloneObjects does not.
                    StyleBase.ExportTo(sourceIds, targetDatabase, Autodesk.Civil.StyleConflictResolverType.Override);
                    groups++;
                }
                continue;
            }

            if (depth < 4 && property.Name.EndsWith("LabelStyles", StringComparison.Ordinal))
                groups += CloneCivilStyleTree(sourceValue, targetValue, sourceDatabase, targetDatabase, copiedOwners, depth + 1);
        }
        return groups;
    }

    private static ObjectIdCollection CollectObjectIds(object collection)
    {
        ObjectIdCollection ids = new ObjectIdCollection();
        IEnumerable enumerable = collection as IEnumerable;
        if (enumerable == null) return ids;
        foreach (object value in enumerable)
            if (value is ObjectId id && !id.IsNull) ids.Add(id);
        return ids;
    }

    private static int ImportPoints(JsonElement root, CivilDocument civil, Database database)
    {
        JsonElement points;
        if (!root.TryGetProperty("points", out points) || points.ValueKind != JsonValueKind.Array)
            return 0;
        HashSet<uint> existing = new HashSet<uint>();
        using (Transaction scan = database.TransactionManager.StartOpenCloseTransaction())
        {
            foreach (ObjectId id in civil.GetAllPointIds())
            {
                CogoPoint point = scan.GetObject(id, OpenMode.ForRead) as CogoPoint;
                if (point != null) existing.Add(point.PointNumber);
            }
            scan.Commit();
        }

        int added = 0;
        foreach (JsonElement item in points.EnumerateArray())
        {
            uint number = (uint)Math.Max(1, ReadInteger(item, "number", 1));
            if (existing.Contains(number)) continue;
            double x = ReadNumber(item, "easting");
            double y = ReadNumber(item, "northing");
            double z = ReadNumber(item, "elevation");
            string description = ReadString(item, "rawDescription");
            // Do not ask Civil 3D to allocate from its next-point-number setting: it can conflict
            // with imported/nonsequential point numbers. The bridge assigns the original number below.
            using (Transaction transaction = database.TransactionManager.StartTransaction())
            {
                ObjectId pointId = civil.CogoPoints.Add(new Point3d(x, y, z), description, false);
                CogoPoint created = transaction.GetObject(pointId, OpenMode.ForWrite) as CogoPoint;
                if (created == null) continue;
                civil.CogoPoints.SetPointNumber(pointId, number);
                ObjectId pointStyle = FindPointStyleId(transaction, civil.Styles.PointStyles, ReadString(item, "pointStyle"));
                if (!pointStyle.IsNull) created.StyleId = pointStyle;
                ObjectId labelStyle = FindPointLabelStyleId(transaction,
                    civil.Styles.LabelStyles.PointLabelStyles.LabelStyles, ReadString(item, "labelStyle"));
                if (!labelStyle.IsNull) created.LabelStyleId = labelStyle;
                transaction.Commit();
            }
            existing.Add(number);
            added++;
        }
        return added;
    }

    private static int ImportSurfaces(JsonElement root, CivilDocument civil, Database database)
    {
        JsonElement surfaces;
        if (!root.TryGetProperty("surfaces", out surfaces) || surfaces.ValueKind != JsonValueKind.Array)
            return 0;
        HashSet<string> existing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using (Transaction scan = database.TransactionManager.StartOpenCloseTransaction())
        {
            foreach (ObjectId id in civil.GetSurfaceIds())
            {
                Autodesk.Civil.DatabaseServices.Surface current =
                    scan.GetObject(id, OpenMode.ForRead) as Autodesk.Civil.DatabaseServices.Surface;
                if (current != null) existing.Add(current.Name);
            }
            scan.Commit();
        }
        int added = 0;
        foreach (JsonElement item in surfaces.EnumerateArray())
        {
            string name = ReadString(item, "name");
            if (String.IsNullOrWhiteSpace(name)) name = "GoSurvey Surface";
            if (existing.Contains(name)) continue;
            JsonElement triangles;
            if (!item.TryGetProperty("triangles", out triangles) || triangles.ValueKind != JsonValueKind.Array)
                continue;
            Point3dCollection vertices = new Point3dCollection();
            HashSet<string> seen = new HashSet<string>(StringComparer.Ordinal);
            List<Point3d[]> sourceTriangles = new List<Point3d[]>();
            foreach (JsonElement triangle in triangles.EnumerateArray())
            {
                if (triangle.ValueKind != JsonValueKind.Array || triangle.GetArrayLength() != 3) continue;
                Point3d[] sourceTriangle = new Point3d[3];
                int triangleVertex = 0;
                foreach (JsonElement vertex in triangle.EnumerateArray())
                {
                    if (vertex.ValueKind != JsonValueKind.Array || vertex.GetArrayLength() != 3) continue;
                    double x = vertex[0].GetDouble(), y = vertex[1].GetDouble(), z = vertex[2].GetDouble();
                    Point3d point = new Point3d(x, y, z);
                    sourceTriangle[triangleVertex++] = point;
                    string key = x.ToString("R", CultureInfo.InvariantCulture) + "," +
                                 y.ToString("R", CultureInfo.InvariantCulture) + "," +
                                 z.ToString("R", CultureInfo.InvariantCulture);
                    if (seen.Add(key)) vertices.Add(point);
                }
                if (triangleVertex == 3) sourceTriangles.Add(sourceTriangle);
            }
            if (vertices.Count < 3) continue;
            using (Transaction transaction = database.TransactionManager.StartTransaction())
            {
                // The database overload uses the target drawing's Civil 3D default surface style.
                // An ObjectId borrowed from a different drawing is not valid for this database.
                ObjectId surfaceId = TinSurface.Create(database, name);
                TinSurface created = transaction.GetObject(surfaceId, OpenMode.ForWrite) as TinSurface;
                if (created == null) continue;
                created.AddVertices(vertices);
                List<Point3dCollection> boundaries = BuildTinBoundaries(sourceTriangles);
                for (int boundaryIndex = 0; boundaryIndex < boundaries.Count; ++boundaryIndex)
                {
                    SurfaceBoundaryType type = boundaryIndex == 0
                        ? SurfaceBoundaryType.Outer : SurfaceBoundaryType.Hide;
                    created.BoundariesDefinition.AddBoundaries(boundaries[boundaryIndex], 1.0, type, true);
                }
                created.Rebuild();
                ObjectId surfaceStyle = FindSurfaceStyleId(transaction, civil.Styles.SurfaceStyles, ReadString(item, "style"));
                if (!surfaceStyle.IsNull) created.StyleId = surfaceStyle;
                transaction.Commit();
            }
            existing.Add(name);
            added++;
        }
        return added;
    }

    private static List<Point3dCollection> BuildTinBoundaries(List<Point3d[]> triangles)
    {
        Dictionary<string, int> edgeUseCounts = new Dictionary<string, int>(StringComparer.Ordinal);
        Dictionary<string, Tuple<string, string>> edgeEnds = new Dictionary<string, Tuple<string, string>>(StringComparer.Ordinal);
        Dictionary<string, Point3d> points = new Dictionary<string, Point3d>(StringComparer.Ordinal);
        foreach (Point3d[] triangle in triangles)
        {
            string[] keys = new string[3];
            for (int i = 0; i < 3; ++i)
            {
                Point3d point = triangle[i];
                keys[i] = point.X.ToString("R", CultureInfo.InvariantCulture) + "," +
                          point.Y.ToString("R", CultureInfo.InvariantCulture);
                points[keys[i]] = point;
            }
            for (int i = 0; i < 3; ++i)
            {
                string first = keys[i], second = keys[(i + 1) % 3];
                string edge = String.CompareOrdinal(first, second) <= 0
                    ? first + "|" + second : second + "|" + first;
                int uses;
                edgeUseCounts.TryGetValue(edge, out uses);
                edgeUseCounts[edge] = uses + 1;
                edgeEnds[edge] = Tuple.Create(first, second);
            }
        }

        Dictionary<string, List<string>> incidentEdges = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        HashSet<string> unusedEdges = new HashSet<string>(StringComparer.Ordinal);
        foreach (KeyValuePair<string, int> entry in edgeUseCounts)
        {
            if (entry.Value != 1) continue;
            unusedEdges.Add(entry.Key);
            Tuple<string, string> ends = edgeEnds[entry.Key];
            if (!incidentEdges.ContainsKey(ends.Item1)) incidentEdges[ends.Item1] = new List<string>();
            if (!incidentEdges.ContainsKey(ends.Item2)) incidentEdges[ends.Item2] = new List<string>();
            incidentEdges[ends.Item1].Add(entry.Key);
            incidentEdges[ends.Item2].Add(entry.Key);
        }

        List<Point3dCollection> result = new List<Point3dCollection>();
        while (unusedEdges.Count > 0)
        {
            string edge = null;
            foreach (string candidate in unusedEdges) { edge = candidate; break; }
            Tuple<string, string> ends = edgeEnds[edge];
            string start = ends.Item1, current = ends.Item2;
            List<string> loop = new List<string> { start, current };
            unusedEdges.Remove(edge);
            while (!String.Equals(current, start, StringComparison.Ordinal))
            {
                List<string> adjacent;
                if (!incidentEdges.TryGetValue(current, out adjacent)) break;
                string nextEdge = null;
                foreach (string candidate in adjacent)
                    if (unusedEdges.Contains(candidate)) { nextEdge = candidate; break; }
                if (nextEdge == null) break;
                unusedEdges.Remove(nextEdge);
                Tuple<string, string> nextEnds = edgeEnds[nextEdge];
                current = String.Equals(nextEnds.Item1, current, StringComparison.Ordinal)
                    ? nextEnds.Item2 : nextEnds.Item1;
                loop.Add(current);
            }
            if (!String.Equals(current, start, StringComparison.Ordinal) || loop.Count < 4) continue;
            Point3dCollection boundary = new Point3dCollection();
            double twiceArea = 0.0;
            for (int i = 0; i < loop.Count; ++i)
            {
                Point3d point = points[loop[i]];
                boundary.Add(point);
                if (i + 1 < loop.Count)
                {
                    Point3d next = points[loop[i + 1]];
                    twiceArea += point.X * next.Y - next.X * point.Y;
                }
            }
            if (Math.Abs(twiceArea) > 0.0) result.Add(boundary);
        }
        result.Sort(delegate(Point3dCollection left, Point3dCollection right)
        {
            return TinBoundaryArea(right).CompareTo(TinBoundaryArea(left));
        });
        return result;
    }

    private static double TinBoundaryArea(Point3dCollection points)
    {
        double twiceArea = 0.0;
        for (int i = 0; i + 1 < points.Count; ++i)
            twiceArea += points[i].X * points[i + 1].Y - points[i + 1].X * points[i].Y;
        return Math.Abs(twiceArea);
    }

    private static int ImportAlignments(JsonElement root, CivilDocument civil, Database database)
    {
        JsonElement alignments;
        if (!root.TryGetProperty("alignments", out alignments) || alignments.ValueKind != JsonValueKind.Array)
            return 0;
        HashSet<string> existing = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using (Transaction scan = database.TransactionManager.StartOpenCloseTransaction())
        {
            foreach (ObjectId id in civil.GetAlignmentIds())
            {
                Alignment alignment = scan.GetObject(id, OpenMode.ForRead) as Alignment;
                if (alignment != null) existing.Add(alignment.Name);
            }
            scan.Commit();
        }

        int added = 0;
        foreach (JsonElement item in alignments.EnumerateArray())
        {
            string name = ReadString(item, "name");
            JsonElement vertices;
            if (String.IsNullOrWhiteSpace(name) || existing.Contains(name) ||
                !item.TryGetProperty("vertices", out vertices) || vertices.ValueKind != JsonValueKind.Array ||
                vertices.GetArrayLength() < 2)
                continue;

            using (Transaction transaction = database.TransactionManager.StartTransaction())
            {
                BlockTable blockTable = (BlockTable)transaction.GetObject(database.BlockTableId, OpenMode.ForRead);
                BlockTableRecord modelSpace = (BlockTableRecord)transaction.GetObject(
                    blockTable[BlockTableRecord.ModelSpace], OpenMode.ForWrite);
                Polyline centerline = new Polyline(vertices.GetArrayLength());
                int index = 0;
                foreach (JsonElement vertex in vertices.EnumerateArray())
                {
                    if (vertex.ValueKind != JsonValueKind.Array || vertex.GetArrayLength() < 2 ||
                        !vertex[0].TryGetDouble(out double x) || !vertex[1].TryGetDouble(out double y))
                        continue;
                    double bulge = vertex.GetArrayLength() > 3 && vertex[3].TryGetDouble(out double value)
                        ? value : 0.0;
                    centerline.AddVertexAt(index++, new Point2d(x, y), bulge, 0.0, 0.0);
                }
                if (centerline.NumberOfVertices < 2) continue;
                ObjectId polylineId = modelSpace.AppendEntity(centerline);
                transaction.AddNewlyCreatedDBObject(centerline, true);
                PolylineOptions options = new PolylineOptions
                {
                    PlineId = polylineId,
                    EraseExistingEntities = true,
                    AddCurvesBetweenTangents = false
                };
                if (civil.Styles.AlignmentStyles.Count == 0 ||
                    civil.Styles.LabelSetStyles.AlignmentLabelSetStyles.Count == 0)
                    continue;
                AlignmentStyle alignmentStyle = transaction.GetObject(
                    civil.Styles.AlignmentStyles[0], OpenMode.ForRead) as AlignmentStyle;
                AlignmentLabelSetStyle labelSetStyle = transaction.GetObject(
                    civil.Styles.LabelSetStyles.AlignmentLabelSetStyles[0], OpenMode.ForRead) as AlignmentLabelSetStyle;
                if (alignmentStyle == null || labelSetStyle == null) continue;
                Alignment.Create(civil, options, name, null, "0", alignmentStyle.Name, labelSetStyle.Name);
                transaction.Commit();
            }
            existing.Add(name);
            added++;
        }
        return added;
    }

    private static ObjectId FindPointStyleId(Transaction transaction, System.Collections.IEnumerable ids, string name)
    {
        if (String.IsNullOrWhiteSpace(name)) return ObjectId.Null;
        foreach (object value in ids)
        {
            ObjectId id = (ObjectId)value;
            PointStyle style = transaction.GetObject(id, OpenMode.ForRead, false) as PointStyle;
            if (style != null && String.Equals(style.Name, name, StringComparison.OrdinalIgnoreCase)) return id;
        }
        return ObjectId.Null;
    }

    private static ObjectId FindPointLabelStyleId(Transaction transaction, System.Collections.IEnumerable ids, string name)
    {
        if (String.IsNullOrWhiteSpace(name)) return ObjectId.Null;
        foreach (object value in ids)
        {
            ObjectId id = (ObjectId)value;
            LabelStyle style = transaction.GetObject(id, OpenMode.ForRead, false) as LabelStyle;
            if (style != null && String.Equals(style.Name, name, StringComparison.OrdinalIgnoreCase)) return id;
        }
        return ObjectId.Null;
    }

    private static ObjectId FindSurfaceStyleId(Transaction transaction, System.Collections.IEnumerable ids, string name)
    {
        if (String.IsNullOrWhiteSpace(name)) return ObjectId.Null;
        foreach (object value in ids)
        {
            ObjectId id = (ObjectId)value;
            SurfaceStyle style = transaction.GetObject(id, OpenMode.ForRead, false) as SurfaceStyle;
            if (style != null && String.Equals(style.Name, name, StringComparison.OrdinalIgnoreCase)) return id;
        }
        return ObjectId.Null;
    }

    private static int ReadInteger(JsonElement item, string name, int fallback)
    {
        JsonElement value;
        return item.TryGetProperty(name, out value) && value.TryGetInt32(out int number) ? number : fallback;
    }

    private static double ReadNumber(JsonElement item, string name)
    {
        JsonElement value;
        return item.TryGetProperty(name, out value) && value.TryGetDouble(out double number) ? number : 0.0;
    }

    private static string ReadString(JsonElement item, string name)
    {
        JsonElement value;
        return item.TryGetProperty(name, out value) && value.ValueKind == JsonValueKind.String ? value.GetString() : String.Empty;
    }

#endif

#if !GOSURVEY_IMPORT_ONLY
    private static string StyleName(Transaction transaction, ObjectId id)
    {
        if (id.IsNull || !id.IsValid) return String.Empty;
        Autodesk.AutoCAD.DatabaseServices.DBObject value = transaction.GetObject(id, OpenMode.ForRead, false);
        if (value is PointStyle pointStyle) return pointStyle.Name;
        if (value is LabelStyle labelStyle) return labelStyle.Name;
        if (value is SurfaceStyle surfaceStyle) return surfaceStyle.Name;
        // Some Civil 3D versions return a database proxy for style IDs accessed through a point
        // group. Read its public Name property without depending on the concrete wrapper type.
        PropertyInfo name = value.GetType().GetProperty("Name", BindingFlags.Instance | BindingFlags.Public);
        if (name != null && name.PropertyType == typeof(string))
        {
            try { return name.GetValue(value) as string ?? String.Empty; }
            catch { }
        }
        return String.Empty;
    }

    private static void AddPoint(StringBuilder json, Point3d point)
    {
        json.Append('[').Append(Number(point.X)).Append(',').Append(Number(point.Y)).Append(',').Append(Number(point.Z)).Append(']');
    }

    private static void AddNumber(StringBuilder json, string name, double value)
    {
        json.Append(',').Append('"').Append(name).Append("\":").Append(Number(value));
    }

    private static string Number(double value) => value.ToString("R", CultureInfo.InvariantCulture);

    private static void AddString(StringBuilder json, string name, string value, bool comma = true)
    {
        if (comma) json.Append(',');
        json.Append('"').Append(name).Append("\":\"");
        foreach (char ch in value ?? String.Empty)
        {
            switch (ch)
            {
                case '"': json.Append("\\\""); break;
                case '\\': json.Append("\\\\"); break;
                case '\n': json.Append("\\n"); break;
                case '\r': json.Append("\\r"); break;
                case '\t': json.Append("\\t"); break;
                default:
                    if (ch < 0x20) json.Append("\\u").Append(((int)ch).ToString("x4", CultureInfo.InvariantCulture));
                    else json.Append(ch);
                    break;
            }
        }
        json.Append('"');
    }
#endif
}
