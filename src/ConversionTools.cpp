// ConversionTools.cpp
// Copyright (c) 2009, Dan Heeks
// This program is released under the BSD license. See the file COPYING for details.
#include "stdafx.h"
#include "ConversionTools.h"
#include <BRepTools.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeShape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include "MarkedList.h"
#include "HLine.h"
#include "HArc.h"
#include "Wire.h"
#include "Face.h"
#include "Edge.h"
#include "Shape.h"
#include "Sketch.h"
#include "Group.h"

void GetConversionMenuTools(std::list<Tool*>* t_list){
	bool lines_or_arcs_in_marked_list = false;
	int sketches_in_marked_list = 0;
	bool group_in_marked_list = false;

	// check to see what types have been marked
	std::list<HeeksObj*>::const_iterator It;
	for(It = wxGetApp().m_marked_list->list().begin(); It != wxGetApp().m_marked_list->list().end(); It++){
		HeeksObj* object = *It;
		switch(object->GetType()){
			case LineType:
			case ArcType:
				lines_or_arcs_in_marked_list = true;
				break;
			case SketchType:
				sketches_in_marked_list++;
				break;
			case GroupType:
				group_in_marked_list = true;
				break;
		}
	}

	if(lines_or_arcs_in_marked_list)
	{
		t_list->push_back(new MakeLineArcsToSketch);
	}

	if(sketches_in_marked_list > 0){
		t_list->push_back(new ConvertSketchToFace);
	}

	if(sketches_in_marked_list > 1){
		t_list->push_back(new CombineSketches);
	}

	if(wxGetApp().m_marked_list->list().size() > 1)t_list->push_back(new GroupSelected);
	if(group_in_marked_list)t_list->push_back(new UngroupSelected);
}

bool ConvertLineArcsToWire2(const std::list<HeeksObj *> &list, TopoDS_Wire &wire)
{
	std::list<TopoDS_Edge> edges;
	std::list<HeeksObj*> list2;
	std::list<HeeksObj*>::const_iterator It;
	for(It = list.begin(); It != list.end(); It++){
		HeeksObj* object = *It;
		if(object->GetType() == SketchType){
			for(HeeksObj* child = object->GetFirstChild(); child; child = object->GetNextChild())
			{
				list2.push_back(child);
			}
		}
		else{
			list2.push_back(object);
		}
	}

	for(std::list<HeeksObj*>::iterator It = list2.begin(); It != list2.end(); It++){
		HeeksObj* object = *It;
		switch(object->GetType()){
			case LineType:
				{
					HLine* line = (HLine*)object;
					edges.push_back(BRepBuilderAPI_MakeEdge(line->A, line->B));
				}
				break;
			case ArcType:
				{
					HArc* arc = (HArc*)object;
					edges.push_back(BRepBuilderAPI_MakeEdge(arc->m_circle, arc->A, arc->B));
				}
				break;
		}
	}

	if(edges.size() > 0){
		BRepBuilderAPI_MakeWire wire_maker;
		std::list<TopoDS_Edge>::iterator It;
		for(It = edges.begin(); It != edges.end(); It++)
		{
			TopoDS_Edge &edge = *It;
			wire_maker.Add(edge);
		}

		wire = wire_maker.Wire();
		return true;
	}

	return false;
}

bool ConvertSketchToFace2(HeeksObj* object, TopoDS_Face& face)
{
	if(object->GetType() != SketchType)return false;
	std::list<HeeksObj*> line_arc_list;

	for(HeeksObj* child = object->GetFirstChild(); child; child = object->GetNextChild())
	{
		line_arc_list.push_back(child);
	}

	std::list<TopoDS_Edge> edges;
	for(std::list<HeeksObj*>::const_iterator It = line_arc_list.begin(); It != line_arc_list.end(); It++){
		HeeksObj* object = *It;
		switch(object->GetType()){
			case LineType:
				{
					HLine* line = (HLine*)object;
					edges.push_back(BRepBuilderAPI_MakeEdge(line->A, line->B));
				}
				break;
			case ArcType:
				{
					HArc* arc = (HArc*)object;
					edges.push_back(BRepBuilderAPI_MakeEdge(arc->m_circle, arc->A, arc->B));
				}
				break;
		}
	}

	if(edges.size() > 0){
		try
		{
			BRepBuilderAPI_MakeWire wire_maker;
			std::list<TopoDS_Edge>::iterator It;
			for(It = edges.begin(); It != edges.end(); It++)
			{
				TopoDS_Edge &edge = *It;
				wire_maker.Add(edge);
			}

			face = BRepBuilderAPI_MakeFace(wire_maker.Wire());
		}
		catch(...)
		{
			wxMessageBox(_("Fatal Error converting sketch to face"));
		}
		return true;
	}

	return false;
}

bool ConvertFaceToSketch2(const TopoDS_Face& face, HeeksObj* sketch)
{
	// given a face, this adds lines and arcs to the given sketch
	// loop through all the loops 
	TopoDS_Wire outerWire=BRepTools::OuterWire(face);

	for (TopExp_Explorer expWire(face, TopAbs_WIRE); expWire.More(); expWire.Next())
	{
		const TopoDS_Shape &W = expWire.Current();
		bool is_outer = W.IsSame(outerWire) != 0;

		for(BRepTools_WireExplorer expEdge(TopoDS::Wire(W)); expEdge.More(); expEdge.Next())
		{
			const TopoDS_Shape &E = expEdge.Current();
			if(!ConvertEdgeToSketch2(TopoDS::Edge(E), sketch))return false;
		}
	}

	return true; // success
}

bool ConvertEdgeToSketch2(const TopoDS_Edge& edge, HeeksObj* sketch)
{
	// enum GeomAbs_CurveType
	// 0 - GeomAbs_Line
	// 1 - GeomAbs_Circle
	// 2 - GeomAbs_Ellipse
	// 3 - GeomAbs_Hyperbola
	// 4 - GeomAbs_Parabola
	// 5 - GeomAbs_BezierCurve
	// 6 - GeomAbs_BSplineCurve
	// 7 - GeomAbs_OtherCurve

	BRepAdaptor_Curve curve(edge);
	GeomAbs_CurveType curve_type = curve.GetType();

	switch(curve_type)
	{
		case GeomAbs_Line:
			// make a line
		{
			double uStart = curve.FirstParameter();
			double uEnd = curve.LastParameter();
			gp_Pnt PS;
			gp_Vec VS;
			curve.D1(uStart, PS, VS);
			gp_Pnt PE;
			gp_Vec VE;
			curve.D1(uEnd, PE, VE);
			HLine* new_object = new HLine(PS, PE, &wxGetApp().current_color);
			sketch->Add(new_object, NULL);
		}
		break;

		case GeomAbs_Circle:
			// make an arc
		{
			double uStart = curve.FirstParameter();
			double uEnd = curve.LastParameter();
			gp_Pnt PS;
			gp_Vec VS;
			curve.D1(uStart, PS, VS);
			gp_Pnt PE;
			gp_Vec VE;
			curve.D1(uEnd, PE, VE);
			gp_Circ circle = curve.Circle();
			HArc* new_object = new HArc(PS, PE, circle, &wxGetApp().current_color);
			sketch->Add(new_object, NULL);
		}
		break;

		default:
		{
			// to do
		}
		break;
	}

	return true;
}

void ConvertSketchToFace::Run(){
	std::list<HeeksObj*>::const_iterator It;
	for(It = wxGetApp().m_marked_list->list().begin(); It != wxGetApp().m_marked_list->list().end(); It++){
		HeeksObj* object = *It;
		if(object->GetType() == SketchType){
			TopoDS_Face face;
			if(ConvertSketchToFace2(object, face))
			{
				wxGetApp().AddUndoably(new CFace(face), NULL, NULL);
				wxGetApp().Repaint();
			}
		}
	}
}

void MakeLineArcsToSketch::Run(){
	std::list<HeeksObj*> objects_to_delete;

	CSketch* sketch = new CSketch();

	std::list<HeeksObj*>::const_iterator It;
	for(It = wxGetApp().m_marked_list->list().begin(); It != wxGetApp().m_marked_list->list().end(); It++){
		HeeksObj* object = *It;
		if(object->GetType() == LineType || object->GetType() == ArcType){
			HeeksObj* new_object = object->MakeACopy();
			objects_to_delete.push_back(object);
			sketch->Add(new_object, NULL);
		}
	}

	wxGetApp().AddUndoably(sketch, NULL, NULL);
	wxGetApp().DeleteUndoably(objects_to_delete);
}

void CombineSketches::Run(){
	CSketch* sketch1 = NULL;
	std::list<HeeksObj*>::const_iterator It;
	std::list<HeeksObj*> copy_of_marked_list = wxGetApp().m_marked_list->list();

	for(It = copy_of_marked_list.begin(); It != copy_of_marked_list.end(); It++){
		HeeksObj* object = *It;
		if(object->GetType() == SketchType){
			if(sketch1)
			{
				std::list<HeeksObj*> new_lines_and_arcs;
				for(HeeksObj* o = object->GetFirstChild(); o; o = object->GetNextChild())
				{
					new_lines_and_arcs.push_back(o->MakeACopy());
				}
				wxGetApp().DeleteUndoably(object);
				wxGetApp().AddUndoably(new_lines_and_arcs, sketch1);
			}
			else
			{
				sketch1 = (CSketch*)object;
			}
		}
	}

	wxGetApp().Repaint();
}

void GroupSelected::Run(){
	if(wxGetApp().m_marked_list->list().size() < 2)
	{
		return;
	}

	CGroup* new_group = new CGroup;
	std::list<HeeksObj*> copy_of_marked_list = wxGetApp().m_marked_list->list();

	wxGetApp().StartHistory();
	wxGetApp().DeleteUndoably(copy_of_marked_list);
	for(std::list<HeeksObj*>::const_iterator It = copy_of_marked_list.begin(); It != copy_of_marked_list.end(); It++){
		HeeksObj* object = *It;
		new_group->Add(object, NULL);
	}
	wxGetApp().AddUndoably(new_group, NULL, NULL);
	wxGetApp().EndHistory();
	wxGetApp().m_marked_list->Clear(true);
	wxGetApp().Repaint();
}

void UngroupSelected::Run(){
	if(wxGetApp().m_marked_list->list().size() == 0)return;

	wxGetApp().StartHistory();
	std::list<HeeksObj*> copy_of_marked_list = wxGetApp().m_marked_list->list();
	std::list<HeeksObj*> to_remove;
	std::list<HeeksObj*> to_add;
	for(std::list<HeeksObj*>::const_iterator It = copy_of_marked_list.begin(); It != copy_of_marked_list.end(); It++){
		HeeksObj* object = *It;
		if(object->GetType() == GroupType)
		{
			for(HeeksObj* o = ((CGroup*)object)->GetFirstChild(); o; o = ((CGroup*)object)->GetNextChild())
			{
				to_add.push_back(o);
				to_remove.push_back(o);
			}
			to_remove.push_back(object);
		}
	}
	wxGetApp().DeleteUndoably(to_remove);
	wxGetApp().AddUndoably(to_add, NULL);
	wxGetApp().EndHistory();

	wxGetApp().m_marked_list->Clear(true);
	wxGetApp().Repaint();
}
