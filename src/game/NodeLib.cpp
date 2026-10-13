// Node type registry: every node available in the graph editor and the command API.
// Names, categories, pins and descriptions are in English (the Atelier graph style). Pin names are
// stored in graph files; NodeDef::findIn / findOut still accept the former French names (Graph.cpp).
#include "Graph.h"

namespace game {

namespace {

struct B {
    NodeDef d;
    B(Op op, const char* id, const char* label, const char* cat, const char* help) {
        d.op = op;
        d.id = id;
        d.label = label;
        d.category = cat;
        d.help = help;
    }
    B& execIn() { return in("", PinType::Exec); }
    B& execOut(const char* n = "") { d.outs.push_back({n, PinType::Exec}); return *this; }
    B& in(const char* n, PinType t) { d.ins.push_back({n, t}); d.defaults.push_back(Value::None(t)); return *this; }
    B& inF(const char* n, float v) { d.ins.push_back({n, PinType::Float}); d.defaults.push_back(Value::Float(v)); return *this; }
    B& inB(const char* n, bool v) { d.ins.push_back({n, PinType::Bool}); d.defaults.push_back(Value::Bool(v)); return *this; }
    B& inV(const char* n, float3 v) { d.ins.push_back({n, PinType::Vec3}); d.defaults.push_back(Value::Vec3(v)); return *this; }
    B& inS(const char* n, const char* v) { d.ins.push_back({n, PinType::String}); d.defaults.push_back(Value::Str(v)); return *this; }
    B& out(const char* n, PinType t) { d.outs.push_back({n, t}); return *this; }
    B& pure() { d.pure = true; return *this; }
    B& event() { d.event = true; return *this; }
};

std::vector<NodeDef> build() {
    const PinType F = PinType::Float, Bo = PinType::Bool, V = PinType::Vec3, O = PinType::Object, S = PinType::String;
    const char* EV = "Events";
    const char* IN = "Input";
    const char* FL = "Flow Control";
    const char* AC = "Scene";
    const char* DA = "Data";
    const char* MA = "Math";
    const char* RT = "RTS";
    std::vector<B> v;
    // events
    v.push_back(B(Op::OnStart, "event.start", "Begin Play", EV, "Runs once when Play starts.").event().execOut());
    v.push_back(B(Op::OnTick, "event.tick", "Event Tick", EV, "Runs every frame; Delta = seconds since the last frame.").event().execOut().out("Delta", F));
    v.push_back(B(Op::OnClickObject, "event.clickObject", "Object Clicked", EV, "Left click on an object during Play.")
                    .event().execOut().out("Object", O).out("Point", V));
    v.push_back(B(Op::OnClickGround, "event.clickGround", "Ground Clicked", EV, "Left click on empty space (ground plane Y = 0) during Play.")
                    .event().execOut().out("Point", V));
    v.push_back(B(Op::OnCustom, "event.custom", "Custom Event", EV, "Runs when a « Call Event » node with the same name is reached.")
                    .event().inS("Name", "MyEvent").execOut());
    v.push_back(B(Op::OnKey, "event.key", "Keyboard Input", IN, "A keyboard key (letter, digit, SPACE, ENTER, F1..F12, UP...).")
                    .event().inS("Key", "R").execOut());
    // flow
    v.push_back(B(Op::Branch, "flow.branch", "Branch", FL, "Continues on True or False depending on the condition.")
                    .execIn().inB("Condition", true).execOut("True").execOut("False"));
    v.push_back(B(Op::Sequence, "flow.sequence", "Sequence", FL, "Runs the outputs in order.")
                    .execIn().execOut("Then 1").execOut("Then 2").execOut("Then 3"));
    v.push_back(B(Op::Delay, "flow.delay", "Delay", FL, "Waits a number of seconds, then continues.")
                    .execIn().inF("Seconds", 1.0f).execOut());
    v.push_back(B(Op::ForEachPart, "flow.forEachPart", "For Each Object", FL, "Loops over the visible objects of a type (empty = all).")
                    .execIn().inS("Type", "CUBE").execOut("Loop").out("Object", O).execOut("Completed"));
    v.push_back(B(Op::DoOnce, "flow.doOnce", "Do Once", FL, "Lets execution pass only once per game.")
                    .execIn().execOut());
    v.push_back(B(Op::FireCustom, "flow.fireCustom", "Call Event", FL, "Triggers the « Custom Event » nodes with this name.")
                    .execIn().inS("Name", "MyEvent").execOut());
    // scene actions
    v.push_back(B(Op::SetPosition, "action.setPosition", "Set Position", AC, "Teleports the object to a position.")
                    .execIn().in("Object", O).inV("Position", float3(0, 0, 0)).execOut());
    v.push_back(B(Op::MoveTo, "action.moveTo", "Move To", AC, "Moves the object towards the target at a constant speed (units/s). Arrived fires on arrival.")
                    .execIn().in("Object", O).inV("Target", float3(0, 0, 0)).inF("Speed", 20.0f).execOut().execOut("Arrived"));
    v.push_back(B(Op::RotateY, "action.rotate", "Rotate Actor", AC, "Rotates the object around the vertical axis (degrees).")
                    .execIn().in("Object", O).inF("Degrees", 45.0f).execOut());
    v.push_back(B(Op::Spawn, "action.spawn", "Spawn Object", AC, "Creates an object from the library (CUBE, SPHERE, BALL).")
                    .execIn().inS("Type", "CUBE").inV("Position", float3(0, 0, 0)).inV("Color", float3(0.8f, 0.2f, 0.2f)).execOut().out("Object", O));
    v.push_back(B(Op::Destroy, "action.destroy", "Destroy Object", AC, "Removes the object from the game (restored on Stop).")
                    .execIn().in("Object", O).execOut());
    v.push_back(B(Op::SetColor, "action.setColor", "Set Color", AC, "Base colour RGB between 0 and 1.")
                    .execIn().in("Object", O).inV("Color", float3(1, 0.8f, 0.2f)).execOut());
    v.push_back(B(Op::SetScale, "action.setScale", "Set Scale", AC, "Object scale (1 = normal size).")
                    .execIn().in("Object", O).inV("Scale", float3(1, 1, 1)).execOut());
    v.push_back(B(Op::Select, "action.select", "Select", AC, "Highlights the object (no object = deselect).")
                    .execIn().in("Object", O).execOut());
    v.push_back(B(Op::Print, "action.print", "Print String", AC, "Shows a text on screen and in the log.")
                    .execIn().inS("Text", "Hello").execOut());
    v.push_back(B(Op::SetVar, "action.setVar", "Set Variable", AC, "Stores a value under a name (global variables of the game).")
                    .execIn().inS("Name", "score").inF("Value", 0).execOut());
    v.push_back(B(Op::AddVar, "action.addVar", "Add to Variable", AC, "Adds an amount to a numeric variable.")
                    .execIn().inS("Name", "score").inF("Amount", 1).execOut());
    // data
    v.push_back(B(Op::ConstFloat, "data.float", "Float", DA, "Constant number.").pure().inF("Value", 1).out("Value", F));
    v.push_back(B(Op::ConstBool, "data.bool", "Boolean", DA, "True or false.").pure().inB("Value", true).out("Value", Bo));
    v.push_back(B(Op::ConstVec3, "data.vec3", "Vector 3", DA, "Constant vector X, Y, Z.").pure().inV("Value", float3(0, 0, 0)).out("Value", V));
    v.push_back(B(Op::ConstString, "data.string", "Text", DA, "Constant text.").pure().inS("Value", "").out("Value", S));
    v.push_back(B(Op::GetVar, "data.getVar", "Get Variable", DA, "Value of a variable (0 if it does not exist).").pure().inS("Name", "score").out("Value", F));
    v.push_back(B(Op::Selected, "data.selected", "Selected Object", DA, "The currently selected object.").pure().out("Object", O));
    v.push_back(B(Op::FindObject, "data.findObject", "Find Object", DA, "First object with this name.").pure().inS("Name", "Cube").out("Object", O));
    v.push_back(B(Op::GetPosition, "data.getPosition", "Get Position", DA, "Position of the object.").pure().in("Object", O).out("Position", V));
    v.push_back(B(Op::PartOf, "data.partOf", "Object Type", DA, "Library identifier (CUBE, SPHERE...).").pure().in("Object", O).out("Type", S));
    v.push_back(B(Op::IsValid, "data.isValid", "Is Valid", DA, "True if the object exists and has not been destroyed.").pure().in("Object", O).out("Valid", Bo));
    v.push_back(B(Op::MakeVec3, "data.makeVec3", "Make Vector 3", DA, "X, Y, Z to a vector.").pure().inF("X", 0).inF("Y", 0).inF("Z", 0).out("Vector", V));
    v.push_back(B(Op::BreakVec3, "data.breakVec3", "Break Vector 3", DA, "Vector to X, Y, Z.").pure().inV("Vector", float3(0, 0, 0)).out("X", F).out("Y", F).out("Z", F));
    v.push_back(B(Op::Distance, "data.distance", "Distance", DA, "Distance between two points.").pure().inV("A", float3(0, 0, 0)).inV("B", float3(0, 0, 0)).out("Distance", F));
    v.push_back(B(Op::Time, "data.time", "Game Time", DA, "Seconds since Play started.").pure().out("Seconds", F));
    // maths / logic
    v.push_back(B(Op::Add, "math.add", "Add", MA, "Addition.").pure().inF("A", 0).inF("B", 0).out("Result", F));
    v.push_back(B(Op::Sub, "math.sub", "Subtract", MA, "Subtraction.").pure().inF("A", 0).inF("B", 0).out("Result", F));
    v.push_back(B(Op::Mul, "math.mul", "Multiply", MA, "Multiplication.").pure().inF("A", 1).inF("B", 1).out("Result", F));
    v.push_back(B(Op::Div, "math.div", "Divide", MA, "Division (0 if B = 0).").pure().inF("A", 1).inF("B", 1).out("Result", F));
    v.push_back(B(Op::Greater, "math.greater", "Greater Than", MA, "Tests whether A is greater than B.").pure().inF("A", 0).inF("B", 0).out("Result", Bo));
    v.push_back(B(Op::Less, "math.less", "Less Than", MA, "Tests whether A is less than B.").pure().inF("A", 0).inF("B", 0).out("Result", Bo));
    v.push_back(B(Op::Equal, "math.equal", "Equal", MA, "Numeric equality.").pure().inF("A", 0).inF("B", 0).out("Result", Bo));
    v.push_back(B(Op::StrEqual, "math.strEqual", "Text Equal", MA, "Compares two texts.").pure().inS("A", "").inS("B", "").out("Result", Bo));
    v.push_back(B(Op::And, "math.and", "And", MA, "True if both are true.").pure().inB("A", true).inB("B", true).out("Result", Bo));
    v.push_back(B(Op::Or, "math.or", "Or", MA, "True if either is true.").pure().inB("A", false).inB("B", false).out("Result", Bo));
    v.push_back(B(Op::Not, "math.not", "Not", MA, "Inverts a boolean.").pure().inB("A", false).out("Result", Bo));
    v.push_back(B(Op::Random, "math.random", "Random", MA, "Random number between Min and Max (a new value each time it is read).").pure().inF("Min", 0).inF("Max", 1).out("Value", F));
    v.push_back(B(Op::AddVec3, "math.addVec3", "Add Vector 3", MA, "Sum of two vectors.").pure().inV("A", float3(0, 0, 0)).inV("B", float3(0, 0, 0)).out("Result", V));
    v.push_back(B(Op::ScaleVec3, "math.scaleVec3", "Scale Vector 3", MA, "Multiplies a vector by a number.").pure().inV("Vector", float3(0, 0, 0)).inF("Factor", 1).out("Result", V));
    v.push_back(B(Op::Concat, "math.concat", "Join Text", MA, "Text A followed by B (accepts numbers and vectors).").pure().inS("A", "").inS("B", "").out("Text", S));
    // RTS
    v.push_back(B(Op::SnapGrid, "rts.snap", "Snap to Grid", RT, "Rounds X and Z to the grid step (Y unchanged).").pure().inV("Position", float3(0, 0, 0)).inF("Step", 6).out("Position", V));
    v.push_back(B(Op::CellFree, "rts.cellFree", "Is Cell Free", RT, "True if no visible object occupies the cell (radius = half a step).")
                    .pure().inV("Position", float3(0, 0, 0)).inF("Step", 6).out("Free", Bo));
    // Native C++ system: the camera, its HUD (+ Add buttons, sun dial), the mouse placement and the sun.
    v.push_back(B(Op::SetRtsCamera, "rts.camera", "RTS Camera", RT, "Enables the RTS camera during play: ZQSD / WASD or arrows, screen edges, wheel, right-drag, Q/E to turn.").execIn().inB("Active", true).execOut());
    v.push_back(B(Op::CountPart, "rts.count", "Count Objects", RT, "Number of visible objects of a type (empty = all).").pure().inS("Type", "CUBE").out("Count", F));
    v.push_back(B(Op::PlaceHouse, "rts.placeHouse", "Place House", RT, "Places a house of the village on a grid cell (Type 0 = small, 1 = large; Cell X, Z = lower corner; Turn 0 to 3). House = its id, -1 if the cell is taken.")
                    .execIn().inF("Type", 0).inF("Cell X", 0).inF("Cell Z", 0).inF("Turn", 0).execOut().out("House", F));
    v.push_back(B(Op::RemoveHouse, "rts.removeHouse", "Remove House", RT, "Removes a house by its id.").execIn().inF("House", -1).execOut());
    v.push_back(B(Op::TurnHouse, "rts.turnHouse", "Turn House", RT, "Turns a house a quarter turn, if the cells allow it.").execIn().inF("House", -1).execOut());
    v.push_back(B(Op::HouseAt, "rts.houseAt", "House At Cell", RT, "Id of the house covering a cell, -1 if none.").pure().inF("Cell X", 0).inF("Cell Z", 0).out("House", F));
    v.push_back(B(Op::HouseCount, "rts.houseCount", "Count Houses", RT, "Number of houses placed.").pure().out("Count", F));
    v.push_back(B(Op::CellAt, "rts.cellAt", "Grid Cell At", RT, "Grid cell (Cell X, Z) under a position: use it with Place House.").pure().inV("Position", float3(0, 0, 0)).out("Cell X", F).out("Cell Z", F));
    v.push_back(B(Op::SetSun, "rts.setSun", "Set Sun", RT, "Sun of the RTS: Azimuth in degrees around the vertical, Height from 10 to 85 degrees.").execIn().inF("Azimuth", 220).inF("Height", 48).execOut());
    v.push_back(B(Op::HudButton, "rts.hudButton", "HUD Button", RT, "Adds a button to the RTS HUD during Play. Id is sent back by « HUD Button Clicked ». Cleared on Stop.").execIn().inS("Id", "small").inS("Label", "+ Add Small House").execOut());
    v.push_back(B(Op::OnHudButton, "event.hudButton", "HUD Button Clicked", EV, "Runs when a button of the RTS HUD is clicked. Id = the Id of the button.").event().execOut().out("Id", S));
    v.push_back(B(Op::OnHouseClicked, "event.houseClicked", "House Clicked", EV, "Left click on a house during Play. House = its id.").event().execOut().out("House", F));
    std::vector<NodeDef> out;
    for (auto& b : v) out.push_back(b.d);
    return out;
}

} // namespace

const std::vector<NodeDef>& nodeDefs() {
    static const std::vector<NodeDef> defs = build();
    return defs;
}

} // namespace game
