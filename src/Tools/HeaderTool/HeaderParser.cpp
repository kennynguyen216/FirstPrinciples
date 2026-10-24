#include "Tools/HeaderTool/HeaderParser.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>

namespace FP::HeaderTool
{
	namespace
	{
		struct Token
		{
			enum Kind { Identifier, Symbol, String, Number } Type;
			std::string Text; // strings without their quotes, escapes resolved
			int Line;
		};

		struct Lexed
		{
			std::vector<Token> Tokens;
			std::map<int, std::string> Comments; // line comment text by line
			std::map<int, bool> CommentOnly;     // lines holding nothing but a comment
		};

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		// Splits source into tokens. Preprocessor lines and block comments are skipped, and line
		// comments are kept by line so they can become descriptions and tooltips.
		Lexed Lex(const std::string& text)
		{
			Lexed out;
			int line = 1;
			bool lineHasCode = false;
			size_t i = 0;
			auto at = [&](size_t index) { return index < text.size() ? text[index] : '\0'; };
			while (i < text.size())
			{
				const char c = text[i];
				if (c == '\n')
				{
					++line;
					lineHasCode = false;
					++i;
				}
				else if (std::isspace(static_cast<unsigned char>(c)))
				{
					++i;
				}
				else if (c == '/' && at(i + 1) == '/')
				{
					const size_t end = text.find('\n', i);
					const std::string comment = Trim(text.substr(i + 2, end == std::string::npos ? std::string::npos : end - i - 2));
					out.Comments[line] = comment;
					out.CommentOnly[line] = !lineHasCode;
					i = end == std::string::npos ? text.size() : end;
				}
				else if (c == '/' && at(i + 1) == '*')
				{
					i += 2;
					while (i < text.size() && !(text[i] == '*' && at(i + 1) == '/'))
					{
						if (text[i] == '\n')
							++line;
						++i;
					}
					i += 2;
				}
				else if (c == '#' && !lineHasCode)
				{
					// Preprocessor line, including any backslash continuations.
					while (i < text.size() && text[i] != '\n')
					{
						if (text[i] == '\\' && at(i + 1) == '\n')
						{
							++line;
							++i;
						}
						++i;
					}
				}
				else if (c == '"' || c == '\'')
				{
					lineHasCode = true;
					const int startLine = line;
					std::string value;
					++i;
					while (i < text.size() && text[i] != c)
					{
						if (text[i] == '\\' && i + 1 < text.size())
						{
							++i;
							value += text[i] == 'n' ? '\n' : text[i];
						}
						else
						{
							if (text[i] == '\n')
								++line;
							value += text[i];
						}
						++i;
					}
					++i;
					out.Tokens.push_back({ Token::String, value, startLine });
				}
				else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
				{
					lineHasCode = true;
					const size_t start = i;
					while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_'))
						++i;
					out.Tokens.push_back({ Token::Identifier, text.substr(start, i - start), line });
				}
				else if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && std::isdigit(static_cast<unsigned char>(at(i + 1)))))
				{
					lineHasCode = true;
					const size_t start = i;
					while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '.' || text[i] == '\''))
						++i;
					out.Tokens.push_back({ Token::Number, text.substr(start, i - start), line });
				}
				else
				{
					lineHasCode = true;
					// :: is the only two character symbol the parser cares about.
					if (c == ':' && at(i + 1) == ':')
					{
						out.Tokens.push_back({ Token::Symbol, "::", line });
						i += 2;
					}
					else
					{
						out.Tokens.push_back({ Token::Symbol, std::string(1, c), line });
						++i;
					}
				}
			}
			return out;
		}

		// Key = value pairs from a marker's parentheses. Flags like ReadOnly have no value.
		struct Meta
		{
			std::map<std::string, std::string> Values;
			bool Has(const std::string& key) const { return Values.count(key) > 0; }
			std::string Get(const std::string& key) const
			{
				const auto found = Values.find(key);
				return found == Values.end() ? std::string() : found->second;
			}
		};

		class Parser
		{
		public:
			Parser(const Lexed& lexed, const std::string& file, std::vector<Class>& classes, std::vector<Error>& errors)
				: m_Lexed(lexed), m_Tokens(lexed.Tokens), m_File(file), m_Classes(classes), m_Errors(errors)
			{
			}

			void Run()
			{
				while (m_Pos < m_Tokens.size())
					Step();
			}

		private:
			struct Scope
			{
				enum Kind { Namespace, Class, Other } Type;
				std::string Name;
				int ClassIndex = -1; // into m_Found for reflected classes
				bool Public = false; // current access inside a class
				bool FriendBody = false;
			};

			const Token* Peek(size_t offset = 0) const { return m_Pos + offset < m_Tokens.size() ? &m_Tokens[m_Pos + offset] : nullptr; }
			bool IsSymbol(size_t offset, const char* symbol) const
			{
				const Token* token = Peek(offset);
				return token && token->Type == Token::Symbol && token->Text == symbol;
			}
			bool IsIdentifier(size_t offset, const char* name) const
			{
				const Token* token = Peek(offset);
				return token && token->Type == Token::Identifier && token->Text == name;
			}

			void Fail(int line, const std::string& message) { m_Errors.push_back({ m_File, line, message }); }

			// The comment lines right above a line, joined, stopping at a blank or code line.
			std::string CommentAbove(int line) const
			{
				std::vector<std::string> lines;
				for (int l = line - 1; l > 0; --l)
				{
					const auto only = m_Lexed.CommentOnly.find(l);
					if (only == m_Lexed.CommentOnly.end() || !only->second)
						break;
					lines.insert(lines.begin(), m_Lexed.Comments.at(l));
				}
				std::string joined;
				for (const std::string& text : lines)
					joined += (joined.empty() ? "" : " ") + text;
				return joined;
			}

			// A comment at the end of a line that also has code on it.
			std::string TrailingComment(int line) const
			{
				const auto only = m_Lexed.CommentOnly.find(line);
				return only != m_Lexed.CommentOnly.end() && !only->second ? m_Lexed.Comments.at(line) : std::string();
			}

			// Reads "(Key = value, Flag)" with the position on the opening parenthesis.
			Meta ReadMeta()
			{
				Meta meta;
				if (!IsSymbol(0, "("))
					return meta;
				++m_Pos;
				int depth = 1;
				std::string key;
				std::string value;
				bool inValue = false;
				auto flush = [&]() {
					if (!key.empty())
						meta.Values[key] = Trim(value);
					key.clear();
					value.clear();
					inValue = false;
				};
				while (m_Pos < m_Tokens.size() && depth > 0)
				{
					const Token& token = m_Tokens[m_Pos++];
					if (token.Type == Token::Symbol && token.Text == "(")
						++depth;
					else if (token.Type == Token::Symbol && token.Text == ")" && --depth == 0)
						break;
					if (depth == 1 && token.Type == Token::Symbol && token.Text == ",")
						flush();
					else if (depth == 1 && token.Type == Token::Symbol && token.Text == "=" && !inValue)
						inValue = true;
					else if (inValue)
						value += (value.empty() || token.Type == Token::String ? "" : " ") + token.Text;
					else if (token.Type == Token::Identifier)
						key = token.Text;
				}
				flush();
				return meta;
			}

			Scope* CurrentClass()
			{
				return !m_Scopes.empty() && m_Scopes.back().Type == Scope::Class ? &m_Scopes.back() : nullptr;
			}

			std::string CurrentNamespace() const
			{
				std::string name;
				for (const Scope& scope : m_Scopes)
				{
					if (scope.Type == Scope::Namespace && !scope.Name.empty())
						name += (name.empty() ? "" : "::") + scope.Name;
				}
				return name;
			}

			bool InsideClass() const
			{
				for (const Scope& scope : m_Scopes)
				{
					if (scope.Type != Scope::Namespace)
						return true;
				}
				return false;
			}

			void Step()
			{
				const Token& token = m_Tokens[m_Pos];
				if (token.Type == Token::Symbol && token.Text == "{")
				{
					m_Scopes.push_back({ Scope::Other, "" });
					++m_Pos;
					return;
				}
				if (token.Type == Token::Symbol && token.Text == "}")
				{
					if (!m_Scopes.empty())
					{
						const Scope closed = m_Scopes.back();
						m_Scopes.pop_back();
						if (closed.Type == Scope::Class && closed.ClassIndex >= 0)
							Finish(closed);
					}
					++m_Pos;
					return;
				}
				if (token.Type != Token::Identifier)
				{
					++m_Pos;
					return;
				}

				if (token.Text == "namespace")
					return ReadNamespace();
				if (token.Text == "FPCLASS")
				{
					m_PendingLine = token.Line;
					++m_Pos;
					m_PendingClass = ReadMeta();
					m_HasPending = true;
					return;
				}
				if (token.Text == "class" || token.Text == "struct")
					return ReadClass();

				Scope* scope = CurrentClass();
				if (scope && (token.Text == "public" || token.Text == "private" || token.Text == "protected") && IsSymbol(1, ":"))
				{
					scope->Public = token.Text == "public";
					m_Pos += 2;
					return;
				}
				if (scope && token.Text == "FP_BODY")
				{
					scope->FriendBody = true;
					++m_Pos;
					return;
				}
				if (token.Text == "FPPROPERTY" || token.Text == "FPFUNCTION")
				{
					if (!scope || scope->ClassIndex < 0)
					{
						Fail(token.Line, token.Text + " is only read inside a script class");
						++m_Pos;
						return;
					}
					++m_Pos;
					const Meta meta = ReadMeta();
					if (token.Text == "FPPROPERTY")
						ReadProperty(*scope, meta, token.Line);
					else
						ReadFunction(*scope, meta, token.Line);
					return;
				}
				// The usual callbacks, noted so the editor can say what a script does.
				if (scope && scope->ClassIndex >= 0 && IsSymbol(1, "(") && token.Text.size() > 2 && token.Text.compare(0, 2, "On") == 0
					&& std::isupper(static_cast<unsigned char>(token.Text[2])))
				{
					std::vector<std::string>& callbacks = m_Found[scope->ClassIndex].Callbacks;
					bool known = false;
					for (const std::string& name : callbacks)
						known |= name == token.Text;
					if (!known)
						callbacks.push_back(token.Text);
				}
				++m_Pos;
			}

			void ReadNamespace()
			{
				++m_Pos;
				std::string name;
				while (const Token* token = Peek())
				{
					if (token->Type == Token::Identifier)
						name += token->Text;
					else if (token->Type == Token::Symbol && token->Text == "::")
						name += "::";
					else
						break;
					++m_Pos;
				}
				if (IsSymbol(0, "{"))
				{
					m_Scopes.push_back({ Scope::Namespace, name });
					++m_Pos;
				}
			}

			void ReadClass()
			{
				const Token& keyword = m_Tokens[m_Pos];
				const bool hadPending = m_HasPending;
				const Meta meta = m_PendingClass;
				const int markerLine = m_HasPending ? m_PendingLine : keyword.Line;
				m_HasPending = false;
				m_PendingClass = {};
				++m_Pos;
				// Skip attributes like alignas or [[nodiscard]] to reach the name.
				std::string name;
				while (const Token* token = Peek())
				{
					if (token->Type == Token::Identifier && token->Text != "final" && token->Text != "alignas")
						name = token->Text;
					if (token->Type == Token::Symbol && (token->Text == ":" || token->Text == "{" || token->Text == ";" || token->Text == "(" || token->Text == ")" || token->Text == ","
						|| token->Text == "*" || token->Text == "&" || token->Text == ">" || token->Text == "<" || token->Text == "="))
						break;
					++m_Pos;
				}
				// Bases, up to the body. A ; first means a forward declaration or a variable.
				bool derivesFromScript = false;
				const size_t afterName = m_Pos;
				while (const Token* token = Peek())
				{
					if (token->Type == Token::Symbol && (token->Text == "{" || token->Text == ";" || token->Text == ")" || token->Text == "="))
						break;
					if (token->Type == Token::Identifier && token->Text == "Script")
						derivesFromScript = true;
					++m_Pos;
				}
				if (!IsSymbol(0, "{") || name.empty())
				{
					if (hadPending)
						Fail(markerLine, "FPCLASS must come right before a class definition");
					m_Pos = afterName;
					return;
				}
				++m_Pos;

				Scope scope{ Scope::Class, name };
				scope.Public = keyword.Text == "struct";
				const bool reflect = (hadPending || derivesFromScript) && !InsideClass();
				if (hadPending && InsideClass())
					Fail(markerLine, "FPCLASS on a class inside another class is not supported. Move it out");
				if (reflect)
				{
					Class found;
					found.Name = name;
					found.Namespace = CurrentNamespace();
					found.IsStruct = keyword.Text == "struct";
					found.Description = meta.Has("Description") ? meta.Get("Description") : CommentAbove(markerLine);
					found.Category = meta.Get("Category");
					found.File = m_File;
					found.Line = keyword.Line;
					scope.ClassIndex = static_cast<int>(m_Found.size());
					m_Found.push_back(found);
				}
				m_Scopes.push_back(scope);
			}

			// The declaration after a marker, up to its ; or body. Returns the declared name.
			std::string ReadDeclarationName(bool function, int& endLine)
			{
				std::string name;
				int depth = 0;
				while (const Token* token = Peek())
				{
					endLine = token->Line;
					if (token->Type == Token::Symbol)
					{
						if (depth == 0 && (token->Text == ";" || token->Text == "{" || token->Text == "=" || token->Text == "[" || (function && token->Text == "(")))
							break;
						if (token->Text == "(" || token->Text == "<")
							++depth;
						else if (token->Text == ")" || token->Text == ">")
							--depth;
					}
					else if (token->Type == Token::Identifier && depth == 0)
					{
						name = token->Text;
					}
					++m_Pos;
				}
				return name;
			}

			void ReadProperty(Scope& scope, const Meta& meta, int line)
			{
				int endLine = line;
				Property property;
				property.Name = ReadDeclarationName(false, endLine);
				property.Line = line;
				if (property.Name.empty())
				{
					Fail(line, "FPPROPERTY must come right before a member variable");
					return;
				}
				if (!scope.Public && !scope.FriendBody)
				{
					Fail(line, "FPPROPERTY " + property.Name + " is not public. Make it public or add FP_BODY() to the class");
					return;
				}
				property.Tooltip = meta.Has("Tooltip") ? meta.Get("Tooltip") : !TrailingComment(endLine).empty() ? TrailingComment(endLine) : CommentAbove(line);
				property.Category = meta.Get("Category");
				property.Asset = meta.Get("Asset");
				property.ReadOnly = meta.Has("ReadOnly");
				if (meta.Has("Range"))
				{
					// "min, max" as one string, or Range = 0, the two numbers split by the comma.
					std::string range = meta.Get("Range");
					for (char& c : range)
						c = c == ',' ? ' ' : c;
					std::istringstream numbers(range);
					if (numbers >> property.Min >> property.Max)
						property.HasRange = true;
					else
						Fail(line, "Range needs two numbers, like Range = \"0, 10\"");
				}
				m_Found[scope.ClassIndex].Properties.push_back(property);
			}

			void ReadFunction(Scope& scope, const Meta& meta, int line)
			{
				int endLine = line;
				Function function;
				function.Name = ReadDeclarationName(true, endLine);
				function.Line = line;
				if (function.Name.empty() || !IsSymbol(0, "("))
				{
					Fail(line, "FPFUNCTION must come right before a member function");
					return;
				}
				if (!IsSymbol(1, ")"))
				{
					Fail(line, "FPFUNCTION " + function.Name + " must take no arguments so the editor can call it");
					return;
				}
				if (!scope.Public && !scope.FriendBody)
				{
					Fail(line, "FPFUNCTION " + function.Name + " is not public. Make it public or add FP_BODY() to the class");
					return;
				}
				function.Tooltip = meta.Has("Tooltip") ? meta.Get("Tooltip") : CommentAbove(line);
				m_Found[scope.ClassIndex].Functions.push_back(function);
			}

			void Finish(const Scope& scope)
			{
				m_Classes.push_back(m_Found[scope.ClassIndex]);
			}

			const Lexed& m_Lexed;
			const std::vector<Token>& m_Tokens;
			std::string m_File;
			std::vector<Class>& m_Classes;
			std::vector<Error>& m_Errors;
			std::vector<Class> m_Found;
			std::vector<Scope> m_Scopes;
			size_t m_Pos = 0;
			Meta m_PendingClass;
			bool m_HasPending = false;
			int m_PendingLine = 0;
		};

		std::string Quote(const std::string& text)
		{
			std::string out = "\"";
			for (const char c : text)
			{
				if (c == '"' || c == '\\')
					out += '\\';
				if (c == '\n')
					out += "\\n";
				else
					out += c;
			}
			return out + "\"";
		}

		std::string Number(float value)
		{
			std::ostringstream out;
			out.precision(9);
			out << value;
			std::string text = out.str();
			if (text.find_first_of(".e") == std::string::npos)
				text += ".0";
			return text + "f";
		}

		std::string Optional(const std::string& text)
		{
			return text.empty() ? "nullptr" : Quote(text);
		}
	}

	void ParseFile(const std::string& text, const std::string& file, std::vector<Class>& classes, std::vector<Error>& errors)
	{
		const Lexed lexed = Lex(text);
		Parser(lexed, file, classes, errors).Run();
	}

	std::string Generate(const std::vector<Class>& classes, const std::string& source)
	{
		std::ostringstream out;
		out << "// Generated by HeaderTool from " << source << ". Do not edit, every build rewrites it.\n";
		out << "#pragma once\n\n";
		out << "#include \"Engine/Scripting/Script.h\"\n";
		for (const Class& type : classes)
		{
			const std::string qualified = (type.Namespace.empty() ? "::" : "::" + type.Namespace + "::") + type.Name;
			out << "\n// " << type.File << ":" << type.Line << "\n";
			if (!type.Namespace.empty())
				out << "namespace " << type.Namespace << " { " << (type.IsStruct ? "struct " : "class ") << type.Name << "; }\n";
			else
				out << (type.IsStruct ? "struct " : "class ") << type.Name << ";\n";

			std::string callbacks;
			for (const std::string& name : type.Callbacks)
				callbacks += (callbacks.empty() ? "" : ", ") + name;

			out << "template <>\n";
			out << "struct FP::ScriptReflection<" << qualified << ">\n{\n";
			out << "\tstatic constexpr const char* Description = " << Optional(type.Description) << ";\n";
			out << "\tstatic constexpr const char* Category = " << Optional(type.Category) << ";\n";
			out << "\tstatic constexpr const char* Callbacks = " << Optional(callbacks) << ";\n\n";
			out << "\ttemplate <typename T = " << qualified << ">\n";
			out << "\tstatic void Describe([[maybe_unused]] ::FP::ScriptTypeBuilder<T>& fields)\n\t{\n";
			for (const Property& property : type.Properties)
			{
				out << "\t\tfields.Field(" << Quote(property.Name) << ", &T::" << property.Name << ")";
				if (!property.Tooltip.empty())
					out << ".Tooltip(" << Quote(property.Tooltip) << ")";
				if (!property.Category.empty())
					out << ".Category(" << Quote(property.Category) << ")";
				if (property.HasRange)
					out << ".Range(" << Number(property.Min) << ", " << Number(property.Max) << ")";
				if (property.ReadOnly)
					out << ".ReadOnly()";
				if (!property.Asset.empty())
					out << ".AssetPath(" << Quote(property.Asset) << ")";
				out << ";\n";
			}
			for (const Function& function : type.Functions)
				out << "\t\tfields.template Function<&T::" << function.Name << ">(" << Quote(function.Name) << ", " << Optional(function.Tooltip) << ");\n";
			out << "\t}\n};\n";
		}
		return out.str();
	}
}
