#pragma once

/// @file xml_to_c.hpp
/// @brief XML signal-flow graph → C code generator (header-only).

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <pugixml.hpp>
#include <stdexcept>

/// XML signal-flow graph → C code generator.
class XmlToC {
public:
    /// Configuration for block function codegen.
    struct BlockTypeConfig {
        /// Block role: "input", "output", or "internal".
        enum class Role { Internal, Input, Output };

        /// XML BlockType attribute value (e.g., "Inport", "Gain").
        std::string_view name;
        /// Block's role in the system (input port, output port, or internal computation).
        Role role;
        /// Function signature for step (e.g., "double sum(double, double, const char*)").
        std::string_view step_signature;
        /// Ordered argument names for step: first inputs (by port), then params (by XML order).
        std::vector<std::string_view> step_arg_names;
        /// Function body for step (without braces).
        std::string_view step_body;
        /// Complete C function implementation for initialization.
        std::string_view function_init;
        /// Number of input edge values for step.
        size_t num_input_args;
        /// Number of block parameters for step.
        size_t num_param_args;
        /// Number of block parameters for init.
        size_t num_init_args;
        /// If true, defer this block's function call to the end of _step().
        bool delay_block;
    };

    /// Array of block function configurations.
    static constexpr size_t CONFIG_SIZE{5};
    static inline const std::array<BlockTypeConfig, CONFIG_SIZE> block_config{{
        {
            .name = "Inport",
            .role = BlockTypeConfig::Role::Input,
            .step_signature = "",
            .step_arg_names = {},
            .step_body = "",
            .function_init = "",
            .num_input_args = 0,
            .num_param_args = 0,
            .num_init_args = 0,
            .delay_block = false
        },
        {
            .name = "Outport",
            .role = BlockTypeConfig::Role::Output,
            .step_signature = "",
            .step_arg_names = {},
            .step_body = "",
            .function_init = "",
            .num_input_args = 0,
            .num_param_args = 0,
            .num_init_args = 0,
            .delay_block = false
        },
        {
            .name = "Sum",
            .role = BlockTypeConfig::Role::Internal,
            .step_signature = "double sum(double, double, const char*)",
            .step_arg_names = {"input1", "input2", "Inputs"},
            .step_body = "\tdouble result = input1;\n\tif (Inputs[1] == '-') result -= input2;\n\telse result += input2;\n\treturn result;",
            .function_init = "",
            .num_input_args = 2,
            .num_param_args = 1,
            .num_init_args = 0,
            .delay_block = false
        },
        {
            .name = "Gain",
            .role = BlockTypeConfig::Role::Internal,
            .step_signature = "double gain(double, double)",
            .step_arg_names = {"input", "Gain"},
            .step_body = "\treturn input * Gain;",
            .function_init = "",
            .num_input_args = 1,
            .num_param_args = 1,
            .num_init_args = 0,
            .delay_block = false
        },
        {
            .name = "UnitDelay",
            .role = BlockTypeConfig::Role::Internal,
            .step_signature = "void delay_update(double*, double)",
            .step_arg_names = {"state_holder", "state"},
            .step_body = "\t*state_holder = state;",
            .function_init = R"(double unitdelay_init() {
    return 0.0;
})",
            .num_input_args = 2,
            .num_param_args = 0,
            .num_init_args = 0,
            .delay_block = true
        }
    }};

    /// Parse XML and generate C source for the given graph.
    /// @param xml XML string describing the signal-flow graph.
    /// @param struct_name Name to use for the generated C struct and functions.
    /// @returns Complete C source as a std::string.
    /// @throws std::runtime_error on malformed or unrecognised input.
    [[nodiscard]] static std::string convert(std::string_view xml, std::string_view struct_name);

    // For testing: expose private members to test friend class
    friend class XmlToCTest;

private:
    /// Block type is represented as an index into block_ops array.
    using BlockType = size_t;
    static constexpr BlockType UNKNOWN_BLOCK_TYPE = block_config.size();

    /// One block in the signal-flow graph.
    struct Block {
        int sid{};
        BlockType type{UNKNOWN_BLOCK_TYPE};
        std::string name;
        std::unordered_map<int, std::string> inputs;  // port# → source field name
        std::unordered_map<std::string, std::string> block_params;  // functional parameters
    };

    /// A directed data-flow edge between two block ports.
    struct Edge {
        int src_sid{};
        int src_port{1};
        int dst_sid{};
        int dst_port{1};
    };

    /// In-memory signal-flow graph produced by the XML parser.
    class Graph {
    private:
        std::unordered_map<int, Block> blocks_;
        std::vector<Edge> edges_;

    public:
        void insert_block(int sid, size_t type, std::string_view name) {
            blocks_[sid] = Block{.sid = sid, .type = type, .name = std::string(name), .inputs = {}, .block_params = {}};
        }

        void insert_edge(int src_sid, int src_port, int dst_sid, int dst_port) {
            edges_.push_back({src_sid, src_port, dst_sid, dst_port});
        }

        void set_block_param(int sid, std::string_view key, std::string_view value) {
            blocks_[sid].block_params[std::string(key)] = std::string(value);
        }

        void set_block_input(int sid, int port, std::string_view src_field) {
            blocks_[sid].inputs[port] = std::string(src_field);
        }

        const auto& blocks() const { return blocks_; }

        const auto& edges() const { return edges_; }

        [[nodiscard]] size_t block_count() const { return blocks_.size(); }

        [[nodiscard]] size_t edge_count() const { return edges_.size(); }
    };

    /// Sanitise block name into a valid C identifier.
    /// Replaces every non-[A-Za-z0-9_] character with '_'; prepends '_' when the
    /// first character would be a digit.
    [[nodiscard]] static std::string sanitise_name(std::string_view raw) {
        std::string out;
        out.reserve(raw.size() + 1);
        for (char c : raw)
            out += (std::isalnum(static_cast<int>(c)) || c == '_') ? c : '_';
        if (!out.empty() && std::isdigit(static_cast<int>(out[0])))
            out.insert(out.begin(), '_');
        return out;
    }

    /// Extract function name from a function definition (e.g., "double foo(...)" → "foo").
    [[nodiscard]] static std::string extract_function_name(std::string_view func_def) {
        if (func_def.empty()) return "";
        auto paren = func_def.find('(');
        if (paren == std::string_view::npos) return "";
        auto last_space = func_def.rfind(' ', paren);
        if (last_space == std::string_view::npos) return "";
        return std::string(func_def.substr(last_space + 1, paren - last_space - 1));
    }

    /// Parse `SID#out:N` or `SID#in:N`. Returns {sid, port}.
    static std::pair<int, int> parse_port_ref(std::string_view ref) {
        auto hash = ref.find('#');
        if (hash == std::string_view::npos)
            throw std::runtime_error("xml_to_c: bad port ref: " + std::string(ref));
        int sid  = std::stoi(std::string(ref.substr(0, hash)));
        auto colon = ref.find(':', hash);
        int port = (colon != std::string_view::npos)
                   ? std::stoi(std::string(ref.substr(colon + 1)))
                   : 1;
        return {sid, port};
    }

    static inline BlockType block_type_from_string(std::string_view s) {
        for (size_t i = 0; i < block_config.size(); ++i)
            if (block_config[i].name == s) return i;
        return UNKNOWN_BLOCK_TYPE;
    }

    /// Parse an XML signal-flow graph into a Graph.
    /// @throws std::runtime_error on malformed or unrecognised input.
    [[nodiscard]] static Graph parse(std::string_view xml) {
        pugi::xml_document doc;
        auto result = doc.load_buffer(xml.data(), xml.size());
        if (!result)
            throw std::runtime_error(std::string("xml_to_c: ") + result.description());

        pugi::xml_node system = doc.child("System");
        if (!system)
            throw std::runtime_error("xml_to_c: expected <System> root element");

        Graph g;

        // ── Blocks ────────────────────────────────────────────────────────────────
        for (pugi::xml_node node : system.children("Block")) {
            int sid = node.attribute("SID").as_int();
            BlockType type = block_type_from_string(node.attribute("BlockType").value());
            std::string name = node.attribute("Name").value();

            g.insert_block(sid, type, name);

            // Only direct <P> children — not those nested inside <Port>
            for (pugi::xml_node p : node.children("P")) {
                std::string_view key = p.attribute("Name").value();
                std::string_view value = p.text().get();
                g.set_block_param(sid, key, value);
            }
        }

        // ── Lines ─────────────────────────────────────────────────────────────────
        for (pugi::xml_node line : system.children("Line")) {
            int src_sid = 0, src_port = 1;

            for (pugi::xml_node p : line.children("P")) {
                if (std::string_view(p.attribute("Name").value()) == "Src") {
                    auto [s, port] = parse_port_ref(p.text().get());
                    src_sid = s; src_port = port;
                }
            }

            // Direct <P Name="Dst"> (no branch)
            for (pugi::xml_node p : line.children("P")) {
                if (std::string_view(p.attribute("Name").value()) == "Dst") {
                    auto [d, dp] = parse_port_ref(p.text().get());
                    g.insert_edge(src_sid, src_port, d, dp);
                    g.set_block_input(d, dp, sanitise_name(g.blocks().at(src_sid).name));
                }
            }

            // Fan-out via <Branch>
            for (pugi::xml_node branch : line.children("Branch")) {
                for (pugi::xml_node p : branch.children("P")) {
                    if (std::string_view(p.attribute("Name").value()) == "Dst") {
                        auto [d, dp] = parse_port_ref(p.text().get());
                        g.insert_edge(src_sid, src_port, d, dp);
                        g.set_block_input(d, dp, sanitise_name(g.blocks().at(src_sid).name));
                    }
                }
            }
        }

        return g;
    }

    /// Kahn's algorithm topological sort over the block graph.
    /// UnitDelay output edges are cut (treated as sources with in-degree 0).
    /// Returns SIDs in execution order; order within the same in-degree level is
    /// by SID for determinism.
    /// @throws std::runtime_error if a cycle is detected.
    [[nodiscard]] static std::vector<int> topo_sort(const Graph& g) {
        std::unordered_map<int, int> in_deg;
        for (auto& [sid, _] : g.blocks()) in_deg[sid] = 0;

        for (auto& e : g.edges()) {
            if (block_config[g.blocks().at(e.src_sid).type].delay_block) continue;
            in_deg[e.dst_sid]++;
        }

        std::vector<int> queue;
        for (auto& [sid, deg] : in_deg)
            if (deg == 0) queue.push_back(sid);
        std::sort(queue.begin(), queue.end());

        std::vector<int> order;
        order.reserve(g.blocks().size());

        while (!queue.empty()) {
            auto it = std::min_element(queue.begin(), queue.end());
            int cur = *it;
            queue.erase(it);
            order.push_back(cur);

            for (auto& e : g.edges()) {
                if (e.src_sid != cur) continue;
                if (block_config[g.blocks().at(cur).type].delay_block) continue;
                if (--in_deg[e.dst_sid] == 0)
                    queue.push_back(e.dst_sid);
            }
        }

        if (order.size() != g.blocks().size())
            throw std::runtime_error("xml_to_c: cycle detected (add UnitDelay to break feedback)");

        return order;
    }

    /// Returns the sanitised C field name for the signal arriving at (dst_sid, dst_port).
    static std::string field_for_input(const Graph& g, int dst_sid, int dst_port) {
        for (auto& e : g.edges())
            if (e.dst_sid == dst_sid && e.dst_port == dst_port) // cppcheck-suppress useStlAlgorithm
                return sanitise_name(g.blocks().at(e.src_sid).name);
        throw std::runtime_error("xml_to_c: no edge driving " + std::to_string(dst_sid) +
                                  "#in:" + std::to_string(dst_port));
    }

    /// Edges into dst_sid sorted by port number for deterministic Sum operand order.
    static std::vector<const Edge*> sorted_inputs(const Graph& g, int dst_sid) {
        std::vector<const Edge*> ins;
        for (auto& e : g.edges())
            if (e.dst_sid == dst_sid) ins.push_back(&e);
        std::sort(ins.begin(), ins.end(),
                  [](const Edge* a, const Edge* b){ return a->dst_port < b->dst_port; });
        return ins;
    }

    /// Gather input field names for a block, sorted by port number.
    static std::vector<std::string> gather_input_args(const Graph& g, int dst_sid, size_t num_inputs) {
        std::vector<std::string> args;
        auto inputs = sorted_inputs(g, dst_sid);
        for (size_t i = 0; i < num_inputs && i < inputs.size(); ++i) {
            args.push_back(sanitise_name(g.blocks().at(inputs[i]->src_sid).name));
        }
        return args;
    }

    /// Gather block parameter values by name.
    static std::vector<std::string> gather_param_args(const Block& b,
                                                      const std::vector<std::string_view>& param_names) {
        std::vector<std::string> args;
        for (const auto& name : param_names) {
            std::string key(name);
            if (b.block_params.count(key))
                args.push_back(b.block_params.at(key));
            else
                args.push_back("");
        }
        return args;
    }

    /// Generate C source for the given graph using `struct_name` as the struct/function name root.
    /// @returns Complete C source as a std::string.
    [[nodiscard]] static std::string generate(const Graph& g, std::string_view struct_name) {
        const auto order = topo_sort(g);
        std::ostringstream out;
        const std::string px(struct_name);

        // ── header ────────────────────────────────────────────────────────────────
        out << "#include \"" << px << "_run.h\"\n";
        out << "#include <stddef.h>\n\n";

        // ── function definitions ──────────────────────────────────────────────────
        for (const auto& cfg : block_config) {
            if (!cfg.step_signature.empty()) {
                // Reconstruct signature with argument names from step_arg_names
                // step_signature is like "double sum(double, double, const char*)"
                // step_arg_names is like {"a", "b", "signs"}
                auto paren_pos = cfg.step_signature.find('(');
                auto close_paren_pos = cfg.step_signature.rfind(')');
                std::string_view sig_prefix = cfg.step_signature.substr(0, paren_pos + 1);
                std::string_view type_str_view = cfg.step_signature.substr(paren_pos + 1, close_paren_pos - paren_pos - 1);
                std::string type_str(type_str_view);

                out << sig_prefix;
                // Split by comma and pair with argument names
                size_t arg_idx = 0;
                size_t pos = 0;
                bool first = true;
                while (pos < type_str.length() && arg_idx < cfg.step_arg_names.size()) {
                    if (!first) out << ", ";
                    first = false;

                    // Find end of this type (next comma or end)
                    size_t comma_pos = type_str.find(',', pos);
                    if (comma_pos == std::string::npos) comma_pos = type_str.length();

                    // Extract type and trim whitespace
                    std::string type_part = type_str.substr(pos, comma_pos - pos);
                    // Trim trailing whitespace
                    while (!type_part.empty() && std::isspace(static_cast<unsigned char>(type_part.back()))) {
                        type_part.pop_back();
                    }
                    // Trim leading whitespace
                    size_t start = 0;
                    while (start < type_part.length() && std::isspace(static_cast<unsigned char>(type_part[start]))) {
                        start++;
                    }
                    type_part = type_part.substr(start);

                    out << type_part << " " << cfg.step_arg_names[arg_idx];

                    pos = comma_pos + 1;
                    arg_idx++;
                }
                out << ") {\n" << cfg.step_body << "\n}\n\n";
            }
            if (!cfg.function_init.empty()) {
                out << cfg.function_init << "\n\n";
            }
        }

        // ── struct ────────────────────────────────────────────────────────────────
        out << "static struct\n{\n";
        for (int sid : order)
            out << "double " << sanitise_name(g.blocks().at(sid).name) << ";\n";
        out << "} " << px << ";\n\n";

        // ── _init() ───────────────────────────────────────────────────────────────
        out << "void " << px << "_generated_init()\n{\n";
        for (auto& [sid, b] : g.blocks()) {
            const auto& cfg = block_config[b.type];
            if (cfg.function_init.empty()) continue;

            const std::string fname = sanitise_name(b.name);
            const std::string init_func_name = extract_function_name(cfg.function_init);

            out << "\t" << px << "." << fname << " = " << init_func_name << "(";

            // Pass parameters based on num_init_args from config
            for (size_t i = 0; i < cfg.num_init_args; ++i) {
                if (i > 0) out << ", ";
                // Get parameter from block's block_params, or default to 0
                // Parameters are numbered 0, 1, 2... matching the block's parameter indices
                if (b.block_params.count(std::to_string(i))) {
                    out << b.block_params.at(std::to_string(i));
                } else {
                    out << "0";
                }
            }

            out << ");\n";
        }
        out << "}\n";
        out << "\n";

        // ── _step() ───────────────────────────────────────────────────────────────
        out << "void " << px << "_generated_step()\n{\n";

        // First pass: process non-deferred blocks
        for (int sid : order) {
            const auto& b     = g.blocks().at(sid);
            const auto& cfg = block_config[b.type];
            const std::string fname = sanitise_name(b.name);

            // Handle output ports explicitly - assign from driving block
            if (cfg.role == BlockTypeConfig::Role::Output) {
                out << "\t" << px << "." << fname << " = " << px << "."
                    << field_for_input(g, sid, 1) << ";\n";
                continue;
            }

            // Skip blocks with no step computation (Inport, Unknown)
            if (cfg.num_input_args == 0 && cfg.num_param_args == 0) continue;

            // Skip deferred blocks in this pass
            if (cfg.delay_block) continue;

            // Gather input arguments from edges
            auto input_args = gather_input_args(g, sid, cfg.num_input_args);
            if (input_args.size() < cfg.num_input_args) {
                throw std::runtime_error("xml_to_c: block " + std::to_string(sid) +
                                        " requires " + std::to_string(cfg.num_input_args) +
                                        " inputs but has " + std::to_string(input_args.size()));
            }

            // Extract function name from signature (e.g., "double sum(double, double, const char*)" → "sum")
            const std::string func_name = extract_function_name(cfg.step_signature);
            out << "\t" << px << "." << fname << " = " << func_name << "(";

            // Gather parameter arguments from block's block_params
            // Only look for parameters that are defined in step_arg_names (after num_input_args)
            std::vector<std::string_view> param_names(cfg.step_arg_names.begin() + static_cast<long>(cfg.num_input_args), cfg.step_arg_names.end());
            auto param_values = gather_param_args(b, param_names);

            // Output arguments in order: inputs first, then parameters
            for (size_t i = 0; i < cfg.step_arg_names.size(); ++i) {
                if (i > 0) out << ", ";

                // First num_input_args are input values (from edges)
                if (i < cfg.num_input_args) {
                    out << px << "." << input_args[i];
                } else {
                    // Remaining are block parameters
                    size_t param_idx = i - cfg.num_input_args;
                    const std::string& val = param_values[param_idx];
                    // Determine if value is numeric (no quotes needed) or string (needs quotes)
                    bool is_numeric = !val.empty() && (std::isdigit(static_cast<int>(val[0])) ||
                                                        (val[0] == '-' && val.length() > 1));
                    if (is_numeric) {
                        out << val;  // Numeric: output as-is
                    } else {
                        out << "\"" << val << "\"";  // String: quote it
                    }
                }
            }

            out << ");\n";
        }

        out << "\n";
        // Second pass: process deferred blocks
        for (int sid : order) {
            const auto& b     = g.blocks().at(sid);
            const auto& cfg = block_config[b.type];
            const std::string fname = sanitise_name(b.name);

            // Skip non-deferred blocks
            if (!cfg.delay_block) continue;

            // For deferred blocks, gather only the edge-driven inputs (not counting the state ptr)
            // Deferred blocks like delay_update take (state_ptr, input_value)
            size_t num_edge_inputs = cfg.num_input_args - 1;  // Subtract 1 for the state ptr
            auto input_args = gather_input_args(g, sid, num_edge_inputs);
            if (input_args.size() < num_edge_inputs) {
                throw std::runtime_error("xml_to_c: deferred block " + std::to_string(sid) +
                                        " requires " + std::to_string(num_edge_inputs) +
                                        " edge inputs but has " + std::to_string(input_args.size()));
            }

            // Extract function name from signature and call with state ptr and gathered inputs
            const std::string func_name = extract_function_name(cfg.step_signature);
            out << "\t" << func_name << "(&" << px << "." << fname << ", " << px << "."
                << input_args[0] << ");\n";
        }
        out << "}\n";
        out << "\n";

        // ── ext_ports[] ───────────────────────────────────────────────────────────
        out << "static const " << px << "_ExtPort\next_ports[] = {\n";
        for (auto& [sid, b] : g.blocks()) {
            const auto& cfg = block_config[b.type];
            int portType = -1;
            if (cfg.role == BlockTypeConfig::Role::Output) {
                portType = 0;
            } else if (cfg.role == BlockTypeConfig::Role::Input) {
                portType = 1;
            } else {
                continue;
            }

            out << "\t{ \"" << b.name << "\", &" << px << "."
                << sanitise_name(b.name) << ", " << portType << " },\n";
        }
        out << "\t{ 0, 0, 0 },\n};\n";
        out << "\n";
        out << "const " << px << "_ExtPort * const\n"
            << px << "_generated_ext_ports = ext_ports;\n"
            << "\n"
            << "const size_t\n"
            << px << "_generated_ext_ports_size = sizeof(ext_ports);\n";

        return out.str();
    }
};

inline std::string XmlToC::convert(std::string_view xml, std::string_view struct_name) {
    return generate(parse(xml), struct_name);
}
