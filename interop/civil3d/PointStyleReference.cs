using System;
using System.IO;
using System.Text.Json;
using System.Collections.Generic;
using Autodesk.AutoCAD.ApplicationServices;
using Autodesk.AutoCAD.DatabaseServices;
using Autodesk.AutoCAD.Runtime;
using Autodesk.Civil.ApplicationServices;
using Autodesk.Civil.DatabaseServices.Styles;
public class StyleInventory {
 [CommandMethod("GSSTYLEINVENTORY")]
 public void Run() {
  var db=Application.DocumentManager.MdiActiveDocument.Database;
  var civil=CivilDocument.GetCivilDocument(db);var rows=new List<Dictionary<string,object>>();
  using(var tr=db.TransactionManager.StartTransaction()) {
   foreach(ObjectId id in civil.Styles.PointStyles) {
    var style=(PointStyle)tr.GetObject(id,OpenMode.ForRead);var row=new Dictionary<string,object>();
    row["handle"]=style.Handle.ToString().ToLowerInvariant();row["name"]=style.Name;
    foreach(var name in new[]{"MarkerType","CustomMarkerStyle","CustomMarkerSuperimposeStyle","SizeType","Display3dType","Orientation","MarkerRotationAngle","MarkerSize","ScaleFactor","Elevation"}) {
     var value=style.GetType().GetProperty(name).GetValue(style);row[name]=value.GetType().IsEnum?(object)Convert.ToInt32(value):value;
    }
    row["markerVisible"]=style.MarkerDisplayStylePlan.Visible;row["labelVisible"]=style.LabelDisplayStylePlan.Visible;
    row["markerLayer"]=style.MarkerDisplayStylePlan.Layer;row["labelLayer"]=style.LabelDisplayStylePlan.Layer;
    var color=style.MarkerDisplayStylePlan.Color; row["colorMethod"]=color.ColorMethod.ToString();row["colorIndex"]=color.ColorIndex;row["argb"]=color.ColorValue.ToArgb();
    rows.Add(row);
   }
   tr.Commit();
  }
  File.WriteAllText(Path.Combine(Path.GetDirectoryName(db.Filename),"all-point-styles.json"),JsonSerializer.Serialize(rows));
 }
}
