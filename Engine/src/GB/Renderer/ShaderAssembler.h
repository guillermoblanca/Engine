#pragma once

// Shader assembler (render M2, DESACOPLE_MATERIALES §2.5 and §2.7): CPU only, no GL.
//
// A source names the pieces it needs with `#include "<name>"` lines; the assembler swaps each such line (its newline included) for the
// piece's text, verbatim, so a program built from pieces is byte for byte the text it would be if written out by hand. Pieces may include
// other pieces. Each piece goes in once per program (a second include of it expands to nothing, like #pragma once); a piece that
// includes itself, directly or through others, is an error, as is a name that is not in the registry.
//
// Variant defines go right after the `#version` line (at the very start if there is none), one `#define <text>` line each, in the order
// given. With no defines the source is not touched there.
//
// The registry is a parameter so the tests can build their own; the engine's own pieces are in Shaders.h (Shaders::Library()).
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GB
{
	struct ShaderPiece
	{
		std::string_view Name;     // e.g. "lib/sky"
		std::string_view Source;   // verbatim text, ending in a newline
	};

	struct ShaderAssembly
	{
		std::string Source;
		std::string Error;   // empty when it worked
		bool Ok() const { return Error.empty(); }
	};

	namespace ShaderAssemblerDetail
	{
		// `line` (without its newline) is `#include "name"`, with optional blanks around; returns the name or an empty view.
		inline std::string_view IncludeName(std::string_view line)
		{
			auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
			size_t i = 0;
			while (i < line.size() && blank(line[i])) ++i;
			constexpr std::string_view directive = "#include";
			if (line.substr(i, directive.size()) != directive) return {};
			i += directive.size();
			const size_t afterDirective = i;
			while (i < line.size() && blank(line[i])) ++i;
			if (i == afterDirective || i >= line.size() || line[i] != '"') return {};
			const size_t open = i + 1;
			const size_t close = line.find('"', open);
			if (close == std::string_view::npos || close == open) return {};
			for (size_t j = close + 1; j < line.size(); ++j)
				if (!blank(line[j])) return {};
			return line.substr(open, close - open);
		}

		inline const ShaderPiece* Find(std::span<const ShaderPiece> pieces, std::string_view name)
		{
			for (const ShaderPiece& p : pieces)
				if (p.Name == name) return &p;
			return nullptr;
		}

		struct State
		{
			std::span<const ShaderPiece> Pieces;
			std::vector<std::string_view> Active;     // the include chain being expanded (cycle detection)
			std::vector<std::string_view> Included;   // pieces already in the program (include once)
			std::string Out;
			std::string Error;
		};

		inline void Expand(State& s, std::string_view source)
		{
			size_t pos = 0;
			while (pos < source.size() && s.Error.empty())
			{
				const size_t eol = source.find('\n', pos);
				const size_t end = eol == std::string_view::npos ? source.size() : eol + 1;
				const std::string_view line = source.substr(pos, (eol == std::string_view::npos ? source.size() : eol) - pos);
				const std::string_view name = IncludeName(line);
				if (name.empty())
				{
					s.Out.append(source.substr(pos, end - pos));
				}
				else
				{
					bool active = false;
					for (std::string_view a : s.Active) active = active || a == name;
					if (active)
					{
						std::string chain;
						for (std::string_view a : s.Active) chain.append(a).append(" -> ");
						s.Error = "shader include cycle: " + chain.append(name);
						return;
					}
					bool done = false;
					for (std::string_view a : s.Included) done = done || a == name;
					if (!done)
					{
						const ShaderPiece* piece = Find(s.Pieces, name);
						if (!piece)
						{
							s.Error = "shader include not found: \"" + std::string(name) + "\"";
							if (!s.Active.empty()) s.Error += " (included from \"" + std::string(s.Active.back()) + "\")";
							return;
						}
						s.Included.push_back(name);
						s.Active.push_back(name);
						Expand(s, piece->Source);
						s.Active.pop_back();
						// A piece without a final newline would glue its last line to the next one.
						if (!piece->Source.empty() && piece->Source.back() != '\n' && end < source.size()) s.Out.push_back('\n');
					}
				}
				pos = end;
			}
		}
	}

	// Resolves the includes of `source` against `pieces` and adds the variant `defines`. On error, Source is empty and Error says why.
	inline ShaderAssembly AssembleShader(std::string_view source, std::span<const std::string_view> defines, std::span<const ShaderPiece> pieces)
	{
		ShaderAssemblerDetail::State s;
		s.Pieces = pieces;
		s.Out.reserve(source.size() * 2);
		ShaderAssemblerDetail::Expand(s, source);
		ShaderAssembly result;
		if (!s.Error.empty())
		{
			result.Error = std::move(s.Error);
			return result;
		}
		if (!defines.empty())
		{
			std::string block;
			for (std::string_view d : defines) block.append("#define ").append(d).append("\n");
			size_t at = 0;
			size_t version = s.Out.compare(0, 8, "#version") == 0 ? 0 : s.Out.find("\n#version");
			if (version != std::string::npos)
			{
				if (version != 0) ++version;
				const size_t eol = s.Out.find('\n', version);
				at = eol == std::string::npos ? s.Out.size() : eol + 1;
				if (eol == std::string::npos) block.insert(0, "\n");
			}
			s.Out.insert(at, block);
		}
		result.Source = std::move(s.Out);
		return result;
	}
}
