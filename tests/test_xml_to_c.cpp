#include "xml_to_c.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <cstdio>

/// Test helper — provides access to XmlToC internal types and functions.
class XmlToCTest {
public:
    using Graph = XmlToC::Graph;
    using Block = XmlToC::Block;
    using Edge = XmlToC::Edge;

    // Named type indices (exposed for testing)
    static constexpr size_t kInport    = 0;
    static constexpr size_t kOutport   = 1;
    static constexpr size_t kSum       = 2;
    static constexpr size_t kGain      = 3;
    static constexpr size_t kUnitDelay = 4;

    static std::string sanitise_name(std::string_view raw) {
        return XmlToC::sanitise_name(raw);
    }

    static size_t block_type(std::string_view name) {
        return XmlToC::block_type_from_string(name);
    }

    static Graph parse(std::string_view xml) {
        return XmlToC::parse(xml);
    }

    static std::vector<int> topo_sort(const Graph& g) {
        return XmlToC::topo_sort(g);
    }

    static std::string generate(const Graph& g, std::string_view struct_name) {
        return XmlToC::generate(g, struct_name);
    }

    static std::pair<int, int> parse_port_ref(std::string_view ref) {
        return XmlToC::parse_port_ref(ref);
    }
};

// ── Block / Edge ──────────────────────────────────────────────────────────────

TEST(DataStructures, SanitiseName) {
    EXPECT_EQ(XmlToCTest::sanitise_name("Unit Delay1"), "Unit_Delay1");
    EXPECT_EQ(XmlToCTest::sanitise_name("Add1"),        "Add1");
    EXPECT_EQ(XmlToCTest::sanitise_name("P_gain"),      "P_gain");
    EXPECT_EQ(XmlToCTest::sanitise_name("1bad"),        "_1bad");
}

TEST(DataStructures, GraphInsertBlockAndEdge) {
    XmlToCTest::Graph g;
    g.insert_block(16, XmlToCTest::kInport, "setpoint");
    g.insert_edge(16, 1, 17, 1);
    EXPECT_EQ(g.block_count(), 1u);
    EXPECT_EQ(g.edge_count(),  1u);
    EXPECT_EQ(g.blocks().at(16).type, XmlToCTest::kInport);
    EXPECT_EQ(g.blocks().at(16).name, "setpoint");
}

// ── XML Parser ────────────────────────────────────────────────────────────────

TEST(XmlParser, ParsesInportBlock) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Inport" Name="setpoint" SID="16">
  <Port><P Name="PortNumber">1</P><P Name="Name">setpoint</P></Port>
</Block>
</System>)";

    auto g = XmlToCTest::parse(xml);
    ASSERT_EQ(g.block_count(), 1u);
    const auto& b = g.blocks().at(16);
    EXPECT_EQ(b.sid,  16);
    EXPECT_EQ(b.type, XmlToCTest::kInport);
    EXPECT_EQ(b.name, "setpoint");
}

TEST(XmlParser, ParsesGainParam) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Gain" Name="P_gain" SID="19"><P Name="Gain">3</P></Block>
</System>)";

    auto g = XmlToCTest::parse(xml);
    EXPECT_EQ(g.blocks().at(19).block_params.at("Gain"), "3");
}

TEST(XmlParser, ParsesSumInputsParam) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Sum" Name="Add1" SID="17"><P Name="Inputs">+-</P></Block>
</System>)";

    auto g = XmlToCTest::parse(xml);
    EXPECT_EQ(g.blocks().at(17).block_params.at("Inputs"), "+-");
}

TEST(XmlParser, ParsesSimpleLine) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Inport" Name="sp" SID="1"/>
<Block BlockType="Sum"    Name="s"  SID="2"/>
<Line><P Name="Src">1#out:1</P><P Name="Dst">2#in:1</P></Line>
</System>)";

    auto g = XmlToCTest::parse(xml);
    ASSERT_EQ(g.edge_count(), 1u);
    EXPECT_EQ(g.edges().at(0).src_sid,  1);
    EXPECT_EQ(g.edges().at(0).src_port, 1);
    EXPECT_EQ(g.edges().at(0).dst_sid,  2);
    EXPECT_EQ(g.edges().at(0).dst_port, 1);
}

TEST(XmlParser, ParsesBranchFanout) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Sum"  Name="A" SID="1"/>
<Block BlockType="Gain" Name="B" SID="2"/>
<Block BlockType="Gain" Name="C" SID="3"/>
<Line>
  <P Name="Src">1#out:1</P>
  <Branch><P Name="Dst">2#in:1</P></Branch>
  <Branch><P Name="Dst">3#in:1</P></Branch>
</Line>
</System>)";

    auto g = XmlToCTest::parse(xml);
    ASSERT_EQ(g.edge_count(), 2u);
    EXPECT_EQ(g.edges().at(0).src_sid, 1);
    EXPECT_EQ(g.edges().at(1).src_sid, 1);
    EXPECT_NE(g.edges().at(0).dst_sid, g.edges().at(1).dst_sid);
}

TEST(XmlParser, RejectsGarbage) {
    EXPECT_THROW(XmlToCTest::parse("not xml at all"), std::runtime_error);
}

TEST(XmlParser, RejectsMissingSystemElement) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<Root>
<Block BlockType="Inport" Name="x" SID="1"/>
</Root>)";
    EXPECT_THROW(XmlToCTest::parse(xml), std::runtime_error);
}

TEST(XmlParser, RejectsBadPortRef_NoHash) {
    constexpr std::string_view xml = R"(<?xml version="1.0"?>
<System>
<Block BlockType="Inport" Name="x" SID="1"/>
<Block BlockType="Gain" Name="y" SID="2"/>
<Line><P Name="Src">1out:1</P><P Name="Dst">2#in:1</P></Line>
</System>)";
    EXPECT_THROW(XmlToCTest::parse(xml), std::runtime_error);
}

TEST(XmlParser, UnknownBlockType) {
    EXPECT_EQ(XmlToCTest::block_type("UnknownBlock"), 5u);
}

TEST(XmlParser, ParsePortRef_WithoutPort) {
    // Port ref "1#out" (no colon and port number) defaults to port 1
    auto [sid, port] = XmlToCTest::parse_port_ref("1#out");
    EXPECT_EQ(sid, 1);
    EXPECT_EQ(port, 1);
}

// ── Topological Sort ──────────────────────────────────────────────────────────

TEST(TopoSort, LinearChain) {
    XmlToCTest::Graph g;
    g.insert_block(1, XmlToCTest::kInport,  "a");
    g.insert_block(2, XmlToCTest::kGain,    "g");
    g.insert_block(3, XmlToCTest::kOutport, "y");
    g.insert_edge(1,1,2,1);
    g.insert_edge(2,1,3,1);

    auto order = XmlToCTest::topo_sort(g);
    ASSERT_EQ(order.size(), 3u);
    auto pos1 = std::find(order.begin(), order.end(), 1) - order.begin();
    auto pos2 = std::find(order.begin(), order.end(), 2) - order.begin();
    auto pos3 = std::find(order.begin(), order.end(), 3) - order.begin();
    EXPECT_LT(pos1, pos2);
    EXPECT_LT(pos2, pos3);
}

TEST(TopoSort, UnitDelayFeedbackCut) {
    XmlToCTest::Graph g;
    g.insert_block(1, XmlToCTest::kInport,    "x");
    g.insert_block(2, XmlToCTest::kSum,        "s");
    g.insert_block(3, XmlToCTest::kGain,       "k");
    g.insert_block(4, XmlToCTest::kUnitDelay,  "d");
    g.insert_block(5, XmlToCTest::kOutport,    "y");
    g.insert_edge(1,1,2,1);
    g.insert_edge(4,1,2,2);
    g.insert_edge(2,1,3,1);
    g.insert_edge(3,1,4,1);
    g.insert_edge(2,1,5,1);

    auto order = XmlToCTest::topo_sort(g);
    EXPECT_EQ(order.size(), 5u);
    auto p4 = std::find(order.begin(), order.end(), 4) - order.begin();
    auto p2 = std::find(order.begin(), order.end(), 2) - order.begin();
    auto p3 = std::find(order.begin(), order.end(), 3) - order.begin();
    EXPECT_LT(p2, p4);
    EXPECT_LT(p3, p4);
}

TEST(TopoSort, ThrowsOnCycleWithoutUnitDelay) {
    XmlToCTest::Graph g;
    g.insert_block(1, XmlToCTest::kSum,  "a");
    g.insert_block(2, XmlToCTest::kGain, "b");
    g.insert_edge(1,1,2,1);
    g.insert_edge(2,1,1,1);

    EXPECT_THROW(XmlToCTest::topo_sort(g), std::runtime_error);
}

TEST(TopoSort, MissingEdgeThrows) {
    XmlToCTest::Graph g;
    g.insert_block(1, XmlToCTest::kInport, "x");
    g.insert_block(2, XmlToCTest::kGain, "y");
    // Insert edge 1->2 but then query for y's input which doesn't exist
    // The field_for_input function will throw when no edge drives the block

    // Create a minimal test by attempting to generate code with unconnected block
    g.insert_block(3, XmlToCTest::kOutport, "z");
    g.insert_edge(2, 1, 3, 1);
    // y (SID 2) has no input, so field_for_input will throw

    EXPECT_THROW(XmlToCTest::generate(g, "test"), std::runtime_error);
}

// ── Code Emitter ──────────────────────────────────────────────────────────────

static constexpr std::string_view PI_XML = R"(<?xml version="1.0" encoding="utf-8"?>
<System>
<Block BlockType="Inport"    Name="setpoint"    SID="16"><Port><P Name="PortNumber">1</P></Port></Block>
<Block BlockType="Inport"    Name="feedback"    SID="18"><Port><P Name="PortNumber">1</P></Port></Block>
<Block BlockType="Sum"       Name="Add1"        SID="17"><P Name="Inputs">+-</P></Block>
<Block BlockType="Sum"       Name="Add2"        SID="22"></Block>
<Block BlockType="Sum"       Name="Add3"        SID="23"></Block>
<Block BlockType="Gain"      Name="I_gain"      SID="25"><P Name="Gain">2</P></Block>
<Block BlockType="Gain"      Name="P_gain"      SID="19"><P Name="Gain">3</P></Block>
<Block BlockType="Gain"      Name="Ts"          SID="26"><P Name="Gain">0.01</P></Block>
<Block BlockType="UnitDelay" Name="Unit Delay1" SID="21"></Block>
<Block BlockType="Outport"   Name="command"     SID="20"></Block>
<Line><P Name="Src">16#out:1</P><P Name="Dst">17#in:1</P></Line>
<Line><P Name="Src">18#out:1</P><P Name="Dst">17#in:2</P></Line>
<Line><P Name="Src">17#out:1</P><Branch><P Name="Dst">25#in:1</P></Branch><Branch><P Name="Dst">19#in:1</P></Branch></Line>
<Line><P Name="Src">21#out:1</P><P Name="Dst">22#in:2</P></Line>
<Line><P Name="Src">22#out:1</P><Branch><P Name="Dst">21#in:1</P></Branch><Branch><P Name="Dst">23#in:2</P></Branch></Line>
<Line><P Name="Src">19#out:1</P><P Name="Dst">23#in:1</P></Line>
<Line><P Name="Src">23#out:1</P><P Name="Dst">20#in:1</P></Line>
<Line><P Name="Src">25#out:1</P><P Name="Dst">26#in:1</P></Line>
<Line><P Name="Src">26#out:1</P><P Name="Dst">22#in:1</P></Line>
</System>)";

TEST(Emitter, StructContainsAllFields) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("double setpoint;"),    std::string::npos);
    EXPECT_NE(result.find("double feedback;"),    std::string::npos);
    EXPECT_NE(result.find("double Unit_Delay1;"), std::string::npos);
    EXPECT_NE(result.find("double Add1;"),        std::string::npos);
    EXPECT_NE(result.find("} controller;"),       std::string::npos);
}

TEST(Emitter, InitResetsOnlyUnitDelays) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("controller.Unit_Delay1 = unitdelay_init()"), std::string::npos);
    EXPECT_EQ(result.find("controller.Add1 = 0"),        std::string::npos);
}

TEST(Emitter, StepComputesCorrectOrder) {
    auto result = XmlToC::convert(PI_XML, "controller");
    // Find the _step() function
    auto step_start = result.find("void controller_generated_step()");
    ASSERT_NE(step_start, std::string::npos);
    // Look for assignments within the step function
    auto pAdd1  = result.find("controller.Add1 =", step_start);
    auto pIgain = result.find("controller.I_gain =", step_start);
    auto pPgain = result.find("controller.P_gain =", step_start);
    auto pAdd3  = result.find("controller.Add3 =", step_start);
    auto pUD    = result.find("delay_update(&controller.Unit_Delay1, controller.Add2)", step_start);
    EXPECT_LT(pAdd1,  pIgain);
    EXPECT_LT(pAdd1,  pPgain);
    EXPECT_LT(pAdd3,  pUD);
}

TEST(Emitter, StepSumWithSigns) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("controller.Add1 = sum(controller.setpoint, controller.feedback, \"+-\")"), std::string::npos);
}

TEST(Emitter, StepGainMultiplies) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("controller.P_gain = gain(controller.Add1, 3)"),      std::string::npos);
    EXPECT_NE(result.find("controller.I_gain = gain(controller.Add1, 2)"),      std::string::npos);
    EXPECT_NE(result.find("controller.Ts = gain(controller.I_gain, 0.01)"),     std::string::npos);
}

TEST(Emitter, ExtPortsTable) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("\"command\""),  std::string::npos);
    EXPECT_NE(result.find("\"setpoint\""), std::string::npos);
    EXPECT_NE(result.find("{ 0, 0, 0 }"), std::string::npos);
}

TEST(Emitter, FullPiControllerGolden) {
    auto result = XmlToC::convert(PI_XML, "controller");
    EXPECT_NE(result.find("void controller_generated_init()"),    std::string::npos);
    EXPECT_NE(result.find("void controller_generated_step()"),    std::string::npos);
    EXPECT_NE(result.find("controller_generated_ext_ports"),      std::string::npos);
    EXPECT_NE(result.find("controller_generated_ext_ports_size"), std::string::npos);
}

TEST(Emitter, GeneratedCCompiles) {
    auto c_src = XmlToC::convert(PI_XML, "controller");

    constexpr std::string_view stub =
        "#include <stddef.h>\n"
        "typedef struct { const char* name; double* ptr; int isInput; } controller_ExtPort;\n";
    auto newline = c_src.find('\n');
    std::string src = std::string(stub) + c_src.substr(newline + 1);

    const std::string tmp = "/tmp/xml_to_c_gen_test.c";
    {
        std::ofstream f(tmp);
        ASSERT_TRUE(f.is_open());
        f << src;
    }
    int rc = std::system(("clang -fsyntax-only " + tmp + " 2>/dev/null").c_str());
    std::remove(tmp.c_str());
    EXPECT_EQ(rc, 0) << "Generated C did not pass clang -fsyntax-only";
}
