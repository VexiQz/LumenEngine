// lumen.regex: backtracking regular expression engine (UTF-8 aware, ECMAScript/PCRE flavoured)
//
// Supported: literals, ., classes [a-z\d], escapes \d \w \s \b \B \A \z \xHH \uHHHH \u{...} \p{L|Lu|Ll|N|P|S|Z},
// groups (...), (?:...), (?<name>...), (?P<name>...), atomic (?>...), lookahead (?=...) (?!...), lookbehind (?<=...) (?<!...),
// alternation, quantifiers * + ? {n,m} with lazy (?) and possessive (+) variants, backreferences \1 \k<name>,
// flags i (ignore case), m (multiline), s (dot matches newline), x (extended: whitespace and #comments ignored).
#include "LuaUtil.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace lumen::lua::detail
{
	namespace
	{
		uint32_t CpLower(uint32_t c)
		{
			if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
			if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
			if (c >= 0x391 && c <= 0x3A9) return c + 32;
			if (c >= 0x410 && c <= 0x42F) return c + 32;
			if (c >= 0x400 && c <= 0x40F) return c + 80;
			return c;
		}
		uint32_t CpUpper(uint32_t c)
		{
			if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
			if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
			if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
			if (c >= 0x430 && c <= 0x44F) return c - 32;
			if (c >= 0x450 && c <= 0x45F) return c - 80;
			return c;
		}
		bool IsLetter(uint32_t c)
		{
			if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			if (c == 0xD7 || c == 0xF7 || c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
			if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0x1F000 && c <= 0x1FAFF) ||
				(c >= 0x300 && c <= 0x36F))
				return false;
			return true;
		}
		bool IsDigit(uint32_t c) { return c >= '0' && c <= '9'; }
		bool IsWordCp(uint32_t c) { return IsDigit(c) || c == '_' || IsLetter(c); }
		bool IsSpaceCp(uint32_t c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x2028 || c == 0x2029 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A) || c == 0xFEFF; }
		bool IsPunct(uint32_t c)
		{
			if (c < 0x80) return (c >= 33 && c <= 47 && c != '$' && c != '+') || (c >= 58 && c <= 64 && c != '<' && c != '=' && c != '>') || (c >= 91 && c <= 96 && c != '^' && c != '`') || c == '{' || c == '}' || c == '_';
			return (c >= 0x2010 && c <= 0x2027) || (c >= 0x3001 && c <= 0x3003) || c == 0xA1 || c == 0xA7 || c == 0xAB || c == 0xB6 || c == 0xB7 || c == 0xBB || c == 0xBF;
		}
		bool IsSymbol(uint32_t c)
		{
			if (c < 0x80) return c == '$' || c == '+' || c == '<' || c == '=' || c == '>' || c == '^' || c == '`' || c == '|' || c == '~';
			return (c >= 0x20A0 && c <= 0x20CF) || (c >= 0x2190 && c <= 0x2BFF) || (c >= 0x1F300 && c <= 0x1FAFF) || c == 0xA2 || c == 0xA3 || c == 0xA5 || c == 0xA9 || c == 0xAE || c == 0xB0 || c == 0xB1;
		}

		uint32_t DecodeCp(const char* s, size_t n, size_t& i)
		{
			const unsigned char c = (unsigned char)s[i];
			if (c < 0x80) { ++i; return c; }
			int len = 0;
			uint32_t cp = 0;
			if ((c >> 5) == 6) { len = 2; cp = c & 0x1f; }
			else if ((c >> 4) == 14) { len = 3; cp = c & 0x0f; }
			else if ((c >> 3) == 30) { len = 4; cp = c & 0x07; }
			else { ++i; return 0xFFFD; }
			if (i + (size_t)len > n) { i = n; return 0xFFFD; }
			for (int k = 1; k < len; ++k)
			{
				const unsigned char cc = (unsigned char)s[i + (size_t)k];
				if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
				cp = (cp << 6) | (cc & 0x3f);
			}
			i += (size_t)len;
			return cp;
		}

		struct ClassItem { char kind; bool negated; }; // d w s L u l N P S Z

		struct Node
		{
			enum Type { Empty, Char, Any, Set, Bol, Eol, WordB, NWordB, TextStart, TextEnd, Group, NonCap, Alt, Seq, Repeat, Backref, Look, Atomic };
			Type t = Empty;
			uint32_t cp = 0;
			std::vector<std::pair<uint32_t, uint32_t>> ranges;
			std::vector<ClassItem> classes;
			bool negate = false;
			int idx = 0;
			std::vector<std::unique_ptr<Node>> kids;
			int min = 0, max = -1;
			bool greedy = true, possessive = false;
			bool behind = false, neg = false;
		};

		struct Prog
		{
			std::unique_ptr<Node> root;
			int groups = 0; // number of capture groups
			std::vector<std::string> names; // names[i] for group i+1 ("" if unnamed)
			bool icase = false, multiline = false, dotall = false;
		};

		class Parser
		{
		public:
			Parser(const std::string& pat, Prog& prog, bool extended) : prog_(prog), ext_(extended)
			{
				size_t i = 0;
				while (i < pat.size()) cps_.push_back(DecodeCp(pat.data(), pat.size(), i));
			}
			bool Parse(std::string& err)
			{
				prog_.root = ParseAlt();
				if (!error_.empty()) { err = error_; return false; }
				if (pos_ < cps_.size()) { err = Err("unmatched ')'"); return false; }
				return true;
			}

		private:
			std::string Err(const std::string& m) { return "regex error at " + std::to_string(pos_) + ": " + m; }
			bool Eof() const { return pos_ >= cps_.size(); }
			uint32_t Peek() const { return cps_[pos_]; }
			bool Fail(const std::string& m) { if (error_.empty()) error_ = Err(m); pos_ = cps_.size(); return false; }

			std::unique_ptr<Node> ParseAlt()
			{
				std::vector<std::unique_ptr<Node>> branches;
				branches.push_back(ParseSeq());
				while (!Eof() && Peek() == '|')
				{
					++pos_;
					branches.push_back(ParseSeq());
				}
				if (branches.size() == 1)
					return std::move(branches[0]);
				auto n = std::make_unique<Node>();
				n->t = Node::Alt;
				n->kids = std::move(branches);
				return n;
			}
			std::unique_ptr<Node> ParseSeq()
			{
				auto seq = std::make_unique<Node>();
				seq->t = Node::Seq;
				while (!Eof() && error_.empty())
				{
					const uint32_t c = Peek();
					if (c == '|' || c == ')') break;
					if (ext_ && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) { ++pos_; continue; }
					if (ext_ && c == '#') { while (!Eof() && Peek() != '\n') ++pos_; continue; }
					std::unique_ptr<Node> atom = ParseAtom();
					if (!atom) break;
					atom = ParseQuantifier(std::move(atom));
					seq->kids.push_back(std::move(atom));
				}
				return seq;
			}
			std::unique_ptr<Node> ParseQuantifier(std::unique_ptr<Node> atom)
			{
				for (;;)
				{
					if (Eof()) return atom;
					int mn, mx;
					const uint32_t c = Peek();
					if (c == '*') { mn = 0; mx = -1; ++pos_; }
					else if (c == '+') { mn = 1; mx = -1; ++pos_; }
					else if (c == '?') { mn = 0; mx = 1; ++pos_; }
					else if (c == '{')
					{
						size_t p = pos_ + 1;
						int a = 0, b = -1;
						bool haveA = false;
						while (p < cps_.size() && IsDigit(cps_[p])) { a = a * 10 + (int)(cps_[p] - '0'); haveA = true; ++p; if (a > 100000) return atom; }
						if (!haveA) return atom;
						if (p < cps_.size() && cps_[p] == ',')
						{
							++p;
							if (p < cps_.size() && IsDigit(cps_[p]))
							{
								b = 0;
								while (p < cps_.size() && IsDigit(cps_[p])) { b = b * 10 + (int)(cps_[p] - '0'); ++p; if (b > 100000) return atom; }
							}
						}
						else b = a;
						if (p >= cps_.size() || cps_[p] != '}') return atom; // literal '{'
						pos_ = p + 1;
						mn = a;
						mx = b;
						if (mx >= 0 && mx < mn) { Fail("bad repetition range"); return atom; }
					}
					else return atom;
					auto rep = std::make_unique<Node>();
					rep->t = Node::Repeat;
					rep->min = mn;
					rep->max = mx;
					if (!Eof() && Peek() == '?') { rep->greedy = false; ++pos_; }
					else if (!Eof() && Peek() == '+') { rep->possessive = true; ++pos_; }
					if (atom->t == Node::Bol || atom->t == Node::Eol || atom->t == Node::WordB || atom->t == Node::NWordB)
					{
						Fail("nothing to repeat");
						return atom;
					}
					rep->kids.push_back(std::move(atom));
					atom = std::move(rep);
				}
			}
			bool ReadHex(int count, uint32_t& out)
			{
				out = 0;
				for (int i = 0; i < count; ++i)
				{
					if (Eof()) return false;
					const uint32_t c = Peek();
					int v;
					if (c >= '0' && c <= '9') v = (int)(c - '0');
					else if (c >= 'a' && c <= 'f') v = (int)(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') v = (int)(c - 'A' + 10);
					else return false;
					out = out * 16 + (uint32_t)v;
					++pos_;
				}
				return true;
			}
			// parses an escape after the backslash; fills either a literal cp or a class item. returns kind: 0 literal, 1 class
			int ParseEscape(uint32_t& cp, ClassItem& cls, bool inClass)
			{
				if (Eof()) { Fail("trailing backslash"); return 0; }
				const uint32_t c = cps_[pos_++];
				switch (c)
				{
				case 'd': case 'D': case 'w': case 'W': case 's': case 'S': cls = {(char)((c | 0x20)), c < 'a'}; return 1;
				case 'n': cp = '\n'; return 0;
				case 'r': cp = '\r'; return 0;
				case 't': cp = '\t'; return 0;
				case 'f': cp = '\f'; return 0;
				case 'v': cp = '\v'; return 0;
				case '0': cp = 0; return 0;
				case 'e': cp = 27; return 0;
				case 'a': cp = 7; return 0;
				case 'x': if (!ReadHex(2, cp)) Fail("bad \\x escape"); return 0;
				case 'u':
					if (!Eof() && Peek() == '{')
					{
						++pos_;
						uint32_t v = 0;
						int n = 0;
						while (!Eof() && Peek() != '}') { uint32_t h; size_t save = pos_; if (!ReadHex(1, h)) { pos_ = save; break; } v = v * 16 + h; ++n; }
						if (Eof() || Peek() != '}' || n == 0) { Fail("bad \\u{} escape"); return 0; }
						++pos_;
						cp = v;
					}
					else if (!ReadHex(4, cp)) Fail("bad \\u escape");
					return 0;
				case 'p': case 'P':
				{
					if (Eof() || Peek() != '{') { Fail("expected \\p{...}"); return 0; }
					++pos_;
					std::string name;
					while (!Eof() && Peek() != '}') name += (char)cps_[pos_++];
					if (Eof()) { Fail("unterminated \\p{...}"); return 0; }
					++pos_;
					char kind = 0;
					if (name == "L" || name == "Letter" || name == "Alpha") kind = 'L';
					else if (name == "Lu" || name == "Upper") kind = 'u';
					else if (name == "Ll" || name == "Lower") kind = 'l';
					else if (name == "N" || name == "Nd" || name == "Digit") kind = 'N';
					else if (name == "P" || name == "Punct") kind = 'P';
					else if (name == "S" || name == "Symbol") kind = 'S';
					else if (name == "Z" || name == "Space") kind = 'Z';
					else { Fail("unknown property \\p{" + name + "}"); return 0; }
					cls = {kind, c == 'P'};
					return 1;
				}
				default:
					if (inClass && c == 'b') { cp = 8; return 0; }
					cp = c;
					return 0;
				}
			}
			std::unique_ptr<Node> ParseClass()
			{
				auto n = std::make_unique<Node>();
				n->t = Node::Set;
				if (!Eof() && Peek() == '^') { n->negate = true; ++pos_; }
				bool first = true;
				while (!Eof() && (Peek() != ']' || first))
				{
					first = false;
					uint32_t lo;
					ClassItem ci{};
					if (Peek() == '\\')
					{
						++pos_;
						if (ParseEscape(lo, ci, true) == 1) { n->classes.push_back(ci); continue; }
					}
					else if (Peek() == '[' && pos_ + 1 < cps_.size() && cps_[pos_ + 1] == ':')
					{
						// POSIX class [:alpha:]
						size_t p = pos_ + 2;
						std::string nm;
						while (p < cps_.size() && cps_[p] != ':' && cps_[p] < 128) nm += (char)cps_[p++];
						if (p + 1 < cps_.size() && cps_[p] == ':' && cps_[p + 1] == ']')
						{
							pos_ = p + 2;
							if (nm == "alpha") n->classes.push_back({'L', false});
							else if (nm == "digit") n->classes.push_back({'d', false});
							else if (nm == "alnum") { n->classes.push_back({'L', false}); n->classes.push_back({'d', false}); }
							else if (nm == "space") n->classes.push_back({'s', false});
							else if (nm == "upper") n->classes.push_back({'u', false});
							else if (nm == "lower") n->classes.push_back({'l', false});
							else if (nm == "punct") n->classes.push_back({'P', false});
							else if (nm == "word") n->classes.push_back({'w', false});
							else if (nm == "xdigit") { n->ranges.push_back({'0', '9'}); n->ranges.push_back({'a', 'f'}); n->ranges.push_back({'A', 'F'}); }
							else { Fail("unknown POSIX class [:" + nm + ":]"); return n; }
							continue;
						}
						lo = cps_[pos_++];
					}
					else
						lo = cps_[pos_++];
					uint32_t hi = lo;
					if (pos_ + 1 < cps_.size() && Peek() == '-' && cps_[pos_ + 1] != ']')
					{
						++pos_;
						if (Peek() == '\\')
						{
							++pos_;
							ClassItem c2{};
							if (ParseEscape(hi, c2, true) == 1) { n->ranges.push_back({lo, lo}); n->ranges.push_back({'-', '-'}); n->classes.push_back(c2); continue; }
						}
						else
							hi = cps_[pos_++];
						if (hi < lo) { Fail("range out of order in character class"); return n; }
					}
					n->ranges.push_back({lo, hi});
				}
				if (Eof()) { Fail("unterminated character class"); return n; }
				++pos_; // ]
				return n;
			}
			std::unique_ptr<Node> ParseAtom()
			{
				const uint32_t c = cps_[pos_++];
				auto n = std::make_unique<Node>();
				switch (c)
				{
				case '.': n->t = Node::Any; return n;
				case '^': n->t = Node::Bol; return n;
				case '$': n->t = Node::Eol; return n;
				case '[': return ParseClass();
				case '(': return ParseGroup();
				case '*': case '+': case '?': Fail("nothing to repeat"); return nullptr;
				case '\\':
				{
					if (Eof()) { Fail("trailing backslash"); return nullptr; }
					const uint32_t e = Peek();
					if (e == 'b') { ++pos_; n->t = Node::WordB; return n; }
					if (e == 'B') { ++pos_; n->t = Node::NWordB; return n; }
					if (e == 'A') { ++pos_; n->t = Node::TextStart; return n; }
					if (e == 'z' || e == 'Z') { ++pos_; n->t = Node::TextEnd; return n; }
					if (e >= '1' && e <= '9')
					{
						int v = 0;
						while (!Eof() && IsDigit(Peek())) { v = v * 10 + (int)(cps_[pos_++] - '0'); if (v > 1000) break; }
						n->t = Node::Backref;
						n->idx = v;
						return n;
					}
					if (e == 'k')
					{
						++pos_;
						if (Eof() || Peek() != '<') { Fail("expected \\k<name>"); return nullptr; }
						++pos_;
						std::string nm;
						while (!Eof() && Peek() != '>') nm += (char)cps_[pos_++];
						if (Eof()) { Fail("unterminated \\k<name>"); return nullptr; }
						++pos_;
						pending_.push_back({n.get(), nm});
						n->t = Node::Backref;
						n->idx = -1;
						return n;
					}
					uint32_t cp = 0;
					ClassItem ci{};
					if (ParseEscape(cp, ci, false) == 1)
					{
						n->t = Node::Set;
						n->classes.push_back(ci);
						return n;
					}
					n->t = Node::Char;
					n->cp = cp;
					return n;
				}
				default:
					n->t = Node::Char;
					n->cp = c;
					return n;
				}
			}
			std::unique_ptr<Node> ParseGroup()
			{
				auto g = std::make_unique<Node>();
				g->t = Node::NonCap;
				if (!Eof() && Peek() == '?')
				{
					++pos_;
					if (Eof()) { Fail("bad group"); return nullptr; }
					const uint32_t k = cps_[pos_++];
					if (k == ':') {}
					else if (k == '>') g->t = Node::Atomic;
					else if (k == '=' || k == '!') { g->t = Node::Look; g->neg = k == '!'; }
					else if (k == '<' && !Eof() && (Peek() == '=' || Peek() == '!')) { g->t = Node::Look; g->behind = true; g->neg = Peek() == '!'; ++pos_; }
					else if (k == '<' || (k == 'P' && !Eof() && Peek() == '<'))
					{
						if (k == 'P') ++pos_;
						std::string nm;
						while (!Eof() && Peek() != '>') nm += (char)cps_[pos_++];
						if (Eof() || nm.empty()) { Fail("bad group name"); return nullptr; }
						++pos_;
						g->t = Node::Group;
						g->idx = ++prog_.groups;
						prog_.names.resize((size_t)prog_.groups);
						prog_.names[(size_t)g->idx - 1] = nm;
					}
					else if (k == 'i' || k == 'm' || k == 's' || k == 'x' || k == '-')
					{
						// inline flags (?i) applying to the whole expression
						pos_--;
						bool on = true;
						while (!Eof() && Peek() != ')')
						{
							const uint32_t f = cps_[pos_++];
							if (f == '-') on = false;
							else if (f == 'i') prog_.icase = on;
							else if (f == 'm') prog_.multiline = on;
							else if (f == 's') prog_.dotall = on;
							else if (f == 'x') ext_ = on;
							else { Fail("unknown inline flag"); return nullptr; }
						}
						if (Eof()) { Fail("unterminated inline flags"); return nullptr; }
						++pos_;
						auto e = std::make_unique<Node>();
						e->t = Node::Empty;
						return e;
					}
					else { Fail("unknown group syntax (?" + std::string(1, (char)k)); return nullptr; }
				}
				else
				{
					g->t = Node::Group;
					g->idx = ++prog_.groups;
					prog_.names.resize((size_t)prog_.groups);
				}
				g->kids.push_back(ParseAlt());
				if (Eof() || Peek() != ')') { Fail("missing ')'"); return nullptr; }
				++pos_;
				return g;
			}

		public:
			void ResolveNames(std::string& err)
			{
				for (auto& p : pending_)
				{
					int idx = -1;
					for (size_t i = 0; i < prog_.names.size(); ++i)
						if (prog_.names[i] == p.second) idx = (int)i + 1;
					if (idx < 0) { err = "regex error: unknown group name '" + p.second + "'"; return; }
					p.first->idx = idx;
				}
			}
			bool BackrefsValid(const Node* n)
			{
				if (n->t == Node::Backref && n->idx > prog_.groups) return false;
				for (const auto& k : n->kids)
					if (!BackrefsValid(k.get())) return false;
				return true;
			}

		private:
			Prog& prog_;
			bool ext_;
			std::vector<uint32_t> cps_;
			size_t pos_ = 0;
			std::string error_;
			std::vector<std::pair<Node*, std::string>> pending_;
		};

		bool ClassMatch(const ClassItem& ci, uint32_t c)
		{
			bool r;
			switch (ci.kind)
			{
			case 'd': r = IsDigit(c); break;
			case 'w': r = IsWordCp(c); break;
			case 's': r = IsSpaceCp(c); break;
			case 'L': r = IsLetter(c); break;
			case 'u': r = IsLetter(c) && CpLower(c) != c; break;
			case 'l': r = IsLetter(c) && CpUpper(c) != c; break;
			case 'N': r = IsDigit(c); break;
			case 'P': r = IsPunct(c); break;
			case 'S': r = IsSymbol(c); break;
			case 'Z': r = IsSpaceCp(c); break;
			default: r = false;
			}
			return ci.negated ? !r : r;
		}

		struct Match
		{
			std::vector<std::pair<long, long>> caps; // [0] whole match, byte offsets; -1 if unset
		};

		class Matcher
		{
		public:
			Matcher(const Prog& p, std::string_view s) : prog_(p), s_(s), caps_((size_t)p.groups + 1, {-1, -1}) {}
			bool aborted = false;

			bool MatchAt(size_t start, Match& out)
			{
				steps_ = 0;
				depth_ = 0;
				std::fill(caps_.begin(), caps_.end(), std::pair<long, long>{-1, -1});
				size_t endPos = 0;
				const bool ok = M(prog_.root.get(), start, [&](size_t e) { endPos = e; return true; });
				if (!ok) return false;
				caps_[0] = {(long)start, (long)endPos};
				out.caps = caps_;
				return true;
			}

		private:
			using Cont = std::function<bool(size_t)>;

			bool Step()
			{
				if (++steps_ > 20000000) aborted = true;
				return !aborted;
			}
			uint32_t CpAt(size_t pos, size_t& next) const
			{
				size_t i = pos;
				const uint32_t c = DecodeCp(s_.data(), s_.size(), i);
				next = i;
				return c;
			}
			bool PrevCp(size_t pos, uint32_t& c) const
			{
				if (pos == 0) return false;
				size_t p = pos - 1;
				while (p > 0 && ((unsigned char)s_[p] & 0xC0) == 0x80) --p;
				size_t i = p;
				c = DecodeCp(s_.data(), s_.size(), i);
				return true;
			}
			bool IsWordAt(size_t pos) const
			{
				uint32_t before = 0, after = 0;
				const bool hasB = PrevCp(pos, before) && IsWordCp(before);
				bool hasA = false;
				if (pos < s_.size()) { size_t n; after = CpAt(pos, n); hasA = IsWordCp(after); }
				return hasA != hasB;
			}
			bool SetMatches(const Node* n, uint32_t c) const
			{
				bool in = false;
				for (const auto& r : n->ranges)
				{
					if (c >= r.first && c <= r.second) { in = true; break; }
					if (prog_.icase)
					{
						const uint32_t l = CpLower(c), u = CpUpper(c);
						if ((l >= r.first && l <= r.second) || (u >= r.first && u <= r.second)) { in = true; break; }
					}
				}
				if (!in)
					for (const ClassItem& ci : n->classes)
						if (ClassMatch(ci, c)) { in = true; break; }
				return in != n->negate;
			}
			// single-char nodes: returns the position after one match or npos
			bool Single(const Node* n, size_t pos, size_t& next) const
			{
				if (pos >= s_.size()) return false;
				const uint32_t c = CpAt(pos, next);
				switch (n->t)
				{
				case Node::Char: return c == n->cp || (prog_.icase && CpLower(c) == CpLower(n->cp));
				case Node::Any: return prog_.dotall || (c != '\n' && c != '\r');
				case Node::Set: return SetMatches(n, c);
				default: return false;
				}
			}
			static bool IsSingle(const Node* n) { return n->t == Node::Char || n->t == Node::Any || n->t == Node::Set; }

			bool M(const Node* n, size_t pos, const Cont& k)
			{
				if (!Step()) return false;
				switch (n->t)
				{
				case Node::Empty: return k(pos);
				case Node::Char: case Node::Any: case Node::Set:
				{
					size_t next;
					return Single(n, pos, next) && k(next);
				}
				case Node::Bol:
				{
					if (pos == 0) return k(pos);
					if (prog_.multiline && (s_[pos - 1] == '\n')) return k(pos);
					return false;
				}
				case Node::Eol:
				{
					if (pos == s_.size()) return k(pos);
					if (prog_.multiline && s_[pos] == '\n') return k(pos);
					if (!prog_.multiline && pos + 1 == s_.size() && s_[pos] == '\n') return false;
					return false;
				}
				case Node::TextStart: return pos == 0 && k(pos);
				case Node::TextEnd: return pos == s_.size() && k(pos);
				case Node::WordB: return IsWordAt(pos) && k(pos);
				case Node::NWordB: return !IsWordAt(pos) && k(pos);
				case Node::Seq: return Seq(n, 0, pos, k);
				case Node::Alt:
					for (const auto& kid : n->kids)
					{
						if (M(kid.get(), pos, k)) return true;
						if (aborted) return false;
					}
					return false;
				case Node::NonCap: return M(n->kids[0].get(), pos, k);
				case Node::Group:
				{
					const auto old = caps_[(size_t)n->idx];
					const bool ok = M(n->kids[0].get(), pos, [&](size_t e) {
						const auto prev = caps_[(size_t)n->idx];
						caps_[(size_t)n->idx] = {(long)pos, (long)e};
						if (k(e)) return true;
						caps_[(size_t)n->idx] = prev;
						return false;
					});
					if (!ok) caps_[(size_t)n->idx] = old;
					return ok;
				}
				case Node::Atomic:
				{
					size_t endPos = 0;
					if (!M(n->kids[0].get(), pos, [&](size_t e) { endPos = e; return true; })) return false;
					return k(endPos);
				}
				case Node::Look:
				{
					bool found;
					if (!n->behind)
						found = M(n->kids[0].get(), pos, [](size_t) { return true; });
					else
					{
						found = false;
						for (size_t st = pos + 1; st-- > 0 && !found;)
						{
							if (st < s_.size() && ((unsigned char)s_[st] & 0xC0) == 0x80) continue;
							found = M(n->kids[0].get(), st, [&](size_t e) { return e == pos; });
							if (aborted) return false;
						}
					}
					if (aborted) return false;
					return (found != n->neg) && k(pos);
				}
				case Node::Backref:
				{
					const auto c = caps_[(size_t)n->idx];
					if (c.first < 0) return false; // unset group never matches
					const size_t len = (size_t)(c.second - c.first);
					if (pos + len > s_.size()) return false;
					if (prog_.icase)
					{
						size_t a = (size_t)c.first, b = pos;
						while (a < (size_t)c.second)
						{
							if (b >= s_.size()) return false;
							const uint32_t x = DecodeCp(s_.data(), s_.size(), a), y = DecodeCp(s_.data(), s_.size(), b);
							if (CpLower(x) != CpLower(y)) return false;
						}
						return k(b);
					}
					if (s_.compare(pos, len, s_.substr((size_t)c.first, len)) != 0) return false;
					return k(pos + len);
				}
				case Node::Repeat: return Rep(n, pos, k);
				}
				return false;
			}

			bool Seq(const Node* n, size_t i, size_t pos, const Cont& k)
			{
				if (i == n->kids.size()) return k(pos);
				if (i + 1 == n->kids.size()) return M(n->kids[i].get(), pos, k);
				if (++depth_ > 5000) { aborted = true; --depth_; return false; }
				const bool r = M(n->kids[i].get(), pos, [&](size_t p) { return Seq(n, i + 1, p, k); });
				--depth_;
				return r;
			}

			bool Rep(const Node* n, size_t pos, const Cont& k)
			{
				const Node* child = n->kids[0].get();
				const int mn = n->min, mx = n->max;
				if (IsSingle(child))
				{
					// iterative fast path: collect match end positions
					std::vector<size_t> ends;
					size_t p = pos;
					int count = 0;
					ends.push_back(p);
					while ((mx < 0 || count < mx))
					{
						size_t next;
						if (!Single(child, p, next)) break;
						p = next;
						++count;
						ends.push_back(p);
						if (!Step()) return false;
					}
					if (count < mn) return false;
					if (n->possessive) return k(ends.back());
					if (n->greedy)
					{
						for (int c = count; c >= mn; --c)
						{
							if (k(ends[(size_t)c])) return true;
							if (aborted) return false;
						}
						return false;
					}
					for (int c = mn; c <= count; ++c)
					{
						if (k(ends[(size_t)c])) return true;
						if (aborted) return false;
					}
					return false;
				}
				if (n->possessive)
				{
					size_t p = pos;
					int count = 0;
					while (mx < 0 || count < mx)
					{
						size_t endPos = 0;
						if (!M(child, p, [&](size_t e) { endPos = e; return true; })) break;
						if (endPos == p) { ++count; break; }
						p = endPos;
						++count;
					}
					return count >= mn && k(p);
				}
				return RepGeneral(n, child, 0, pos, k);
			}
			bool RepGeneral(const Node* n, const Node* child, int count, size_t pos, const Cont& k)
			{
				if (!Step()) return false;
				if (++depth_ > 5000) { aborted = true; --depth_; return false; }
				bool r = false;
				const bool canMore = n->max < 0 || count < n->max;
				auto more = [&]() {
					return canMore && M(child, pos, [&](size_t p) {
						if (p == pos && count >= n->min) return false; // empty iteration: stop looping
						return RepGeneral(n, child, count + 1, p, k);
					});
				};
				if (n->greedy)
					r = more() || (!aborted && count >= n->min && k(pos));
				else
					r = (count >= n->min && k(pos)) || (!aborted && more());
				--depth_;
				return r;
			}

			const Prog& prog_;
			std::string_view s_;
			std::vector<std::pair<long, long>> caps_;
			long steps_ = 0;
			int depth_ = 0;
		};

		// ------------------------------------------------------------ Lua binding
		struct Regex
		{
			std::shared_ptr<Prog> prog;
			std::string source, flags;
		};
		const char* kRegex = "lumen.regex";

		std::shared_ptr<Prog> Compile(const std::string& pattern, const std::string& flags, std::string& err)
		{
			auto p = std::make_shared<Prog>();
			bool ext = false;
			for (char f : flags)
			{
				if (f == 'i') p->icase = true;
				else if (f == 'm') p->multiline = true;
				else if (f == 's') p->dotall = true;
				else if (f == 'x') ext = true;
				else if (f == 'u' || f == 'g') {}
				else { err = std::string("unknown regex flag '") + f + "'"; return nullptr; }
			}
			Parser parser(pattern, *p, ext);
			if (!parser.Parse(err)) return nullptr;
			parser.ResolveNames(err);
			if (!err.empty()) return nullptr;
			if (!parser.BackrefsValid(p->root.get())) { err = "regex error: backreference to a group that does not exist"; return nullptr; }
			p->names.resize((size_t)p->groups);
			return p;
		}

		std::shared_ptr<Prog> Cached(const std::string& pattern, const std::string& flags, std::string& err)
		{
			static std::mutex mu;
			static std::map<std::string, std::shared_ptr<Prog>> cache;
			const std::string key = flags + "/" + pattern;
			{
				std::lock_guard<std::mutex> lock(mu);
				auto it = cache.find(key);
				if (it != cache.end()) return it->second;
			}
			auto p = Compile(pattern, flags, err);
			if (p)
			{
				std::lock_guard<std::mutex> lock(mu);
				if (cache.size() > 256) cache.clear();
				cache[key] = p;
			}
			return p;
		}

		// Obtains the regex from arg `i`: a compiled object or a pattern string (module-level convenience).
		std::shared_ptr<Prog> RegexArg(lua_State* L, int i, bool& module)
		{
			if (Regex* r = static_cast<Regex*>(luaL_testudata(L, i, kRegex)))
			{
				module = false;
				return r->prog;
			}
			module = true;
			const std::string pat = Str(L, i);
			std::string err;
			auto p = Cached(pat, "", err);
			if (!p) luaL_error(L, "%s", err.c_str());
			return p;
		}

		bool FindFrom(const Prog& prog, std::string_view s, size_t from, Match& m, bool& aborted)
		{
			Matcher mt(prog, s);
			for (size_t pos = from; pos <= s.size(); ++pos)
			{
				if (pos < s.size() && pos > from && ((unsigned char)s[pos] & 0xC0) == 0x80) continue;
				if (mt.MatchAt(pos, m)) return true;
				if (mt.aborted) { aborted = true; return false; }
			}
			return false;
		}

		void PushMatch(lua_State* L, const Prog& prog, std::string_view s, const Match& m)
		{
			lua_newtable(L);
			Push(L, s.substr((size_t)m.caps[0].first, (size_t)(m.caps[0].second - m.caps[0].first)));
			lua_setfield(L, -2, "text");
			lua_pushinteger(L, m.caps[0].first + 1);
			lua_setfield(L, -2, "start");
			lua_pushinteger(L, m.caps[0].second);
			lua_setfield(L, -2, "stop");
			lua_newtable(L); // named groups
			for (int i = 1; i <= prog.groups; ++i)
			{
				const auto c = m.caps[(size_t)i];
				if (c.first >= 0)
					Push(L, s.substr((size_t)c.first, (size_t)(c.second - c.first)));
				else
					lua_pushboolean(L, 0);
				lua_pushvalue(L, -1);
				lua_rawseti(L, -4, i);
				if (!prog.names[(size_t)i - 1].empty())
				{
					lua_setfield(L, -2, prog.names[(size_t)i - 1].c_str());
				}
				else
					lua_pop(L, 1);
			}
			lua_setfield(L, -2, "named");
		}

		size_t InitPos(lua_State* L, int i, std::string_view s)
		{
			lua_Integer init = luaL_optinteger(L, i, 1);
			if (init < 0) init = (lua_Integer)s.size() + init + 1;
			if (init < 1) init = 1;
			return (size_t)init - 1;
		}

		int RxCompile(lua_State* L)
		{
			const std::string pat = Str(L, 1);
			const std::string flags = lua_isnoneornil(L, 2) ? "" : Str(L, 2);
			std::string err;
			auto p = Cached(pat, flags, err);
			if (!p) return Fail(L, err);
			Regex* r = NewUdata<Regex>(L, kRegex);
			r->prog = p;
			r->source = pat;
			r->flags = flags;
			return 1;
		}
		int RxTest(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			Match m;
			bool aborted = false;
			const bool ok = FindFrom(*p, s, InitPos(L, 3, s), m, aborted);
			if (aborted) return luaL_error(L, "regex too complex");
			lua_pushboolean(L, ok);
			return 1;
		}
		// find(s, init) -> start, stop, captures...
		int RxFind(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			Match m;
			bool aborted = false;
			if (!FindFrom(*p, s, InitPos(L, 3, s), m, aborted))
			{
				if (aborted) return luaL_error(L, "regex too complex");
				lua_pushnil(L);
				return 1;
			}
			lua_pushinteger(L, m.caps[0].first + 1);
			lua_pushinteger(L, m.caps[0].second);
			for (int i = 1; i <= p->groups; ++i)
			{
				const auto c = m.caps[(size_t)i];
				if (c.first >= 0) Push(L, s.substr((size_t)c.first, (size_t)(c.second - c.first))); else lua_pushboolean(L, 0);
			}
			return 2 + p->groups;
		}
		int RxMatch(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			Match m;
			bool aborted = false;
			if (!FindFrom(*p, s, InitPos(L, 3, s), m, aborted))
			{
				if (aborted) return luaL_error(L, "regex too complex");
				lua_pushnil(L);
				return 1;
			}
			PushMatch(L, *p, s, m);
			return 1;
		}
		int RxFindAll(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			const lua_Integer limit = luaL_optinteger(L, 3, -1);
			lua_newtable(L);
			size_t pos = 0;
			lua_Integer n = 0;
			while (pos <= s.size() && (limit < 0 || n < limit))
			{
				Match m;
				bool aborted = false;
				if (!FindFrom(*p, s, pos, m, aborted))
				{
					if (aborted) return luaL_error(L, "regex too complex");
					break;
				}
				PushMatch(L, *p, s, m);
				lua_rawseti(L, -2, ++n);
				if (m.caps[0].second == m.caps[0].first)
				{
					size_t i = (size_t)m.caps[0].second;
					if (i < s.size()) DecodeCp(s.data(), s.size(), i); else i = s.size() + 1;
					pos = i;
				}
				else
					pos = (size_t)m.caps[0].second;
			}
			return 1;
		}
		int GmatchIter(lua_State* L)
		{
			// upvalues: 1 regex (object or pattern), 2 string, 3 position (integer, -1 when done)
			bool mod;
			auto p = RegexArg(L, lua_upvalueindex(1), mod);
			const std::string_view s = View(L, lua_upvalueindex(2));
			lua_Integer pos = lua_tointeger(L, lua_upvalueindex(3));
			if (pos < 0 || (size_t)pos > s.size()) return 0;
			Match m;
			bool aborted = false;
			if (!FindFrom(*p, s, (size_t)pos, m, aborted))
			{
				if (aborted) return luaL_error(L, "regex too complex");
				lua_pushinteger(L, -1);
				lua_replace(L, lua_upvalueindex(3));
				return 0;
			}
			size_t next = (size_t)m.caps[0].second;
			if (m.caps[0].second == m.caps[0].first)
			{
				size_t i = next;
				if (i < s.size()) DecodeCp(s.data(), s.size(), i); else i = s.size() + 1;
				next = i;
			}
			lua_pushinteger(L, (lua_Integer)next);
			lua_replace(L, lua_upvalueindex(3));
			PushMatch(L, *p, s, m);
			return 1;
		}
		int RxGmatch(lua_State* L)
		{
			luaL_checkany(L, 1);
			luaL_checkstring(L, 2);
			lua_settop(L, 2);
			lua_pushinteger(L, 0);
			lua_pushcclosure(L, GmatchIter, 3);
			return 1;
		}
		std::string Expand(const std::string& tpl, const Prog& prog, std::string_view s, const Match& m)
		{
			std::string out;
			auto group = [&](int i) {
				const auto c = m.caps[(size_t)i];
				return c.first >= 0 ? std::string(s.substr((size_t)c.first, (size_t)(c.second - c.first))) : std::string();
			};
			for (size_t i = 0; i < tpl.size(); ++i)
			{
				if (tpl[i] != '$' || i + 1 >= tpl.size()) { out += tpl[i]; continue; }
				const char c = tpl[i + 1];
				if (c == '$') { out += '$'; ++i; }
				else if (c == '&') { out += group(0); ++i; }
				else if (isdigit((unsigned char)c))
				{
					int v = c - '0';
					size_t j = i + 2;
					if (j < tpl.size() && isdigit((unsigned char)tpl[j]) && v * 10 + (tpl[j] - '0') <= prog.groups) { v = v * 10 + (tpl[j] - '0'); ++j; }
					if (v <= prog.groups) { out += group(v); i = j - 1; }
					else out += '$';
				}
				else if (c == '{')
				{
					const size_t e = tpl.find('}', i + 2);
					if (e == std::string::npos) { out += '$'; continue; }
					const std::string name = tpl.substr(i + 2, e - i - 2);
					int idx = -1;
					if (!name.empty() && isdigit((unsigned char)name[0])) idx = atoi(name.c_str());
					else
						for (size_t g = 0; g < prog.names.size(); ++g)
							if (prog.names[g] == name) idx = (int)g + 1;
					if (idx >= 0 && idx <= prog.groups) out += group(idx);
					i = e;
				}
				else out += '$';
			}
			return out;
		}
		// gsub(s, repl, limit) repl: template string | function(match) | table
		int RxGsub(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			const int rt = lua_type(L, 3);
			luaL_argcheck(L, rt == LUA_TSTRING || rt == LUA_TFUNCTION || rt == LUA_TTABLE || rt == LUA_TNUMBER, 3, "replacement must be a string, function or table");
			const lua_Integer limit = luaL_optinteger(L, 4, -1);
			std::string out;
			size_t pos = 0, last = 0;
			lua_Integer count = 0;
			while (pos <= s.size() && (limit < 0 || count < limit))
			{
				Match m;
				bool aborted = false;
				if (!FindFrom(*p, s, pos, m, aborted))
				{
					if (aborted) return luaL_error(L, "regex too complex");
					break;
				}
				const size_t ms = (size_t)m.caps[0].first, me = (size_t)m.caps[0].second;
				out.append(s.substr(last, ms - last));
				const std::string_view whole = s.substr(ms, me - ms);
				if (rt == LUA_TSTRING || rt == LUA_TNUMBER)
					out += Expand(lua_tostring(L, 3), *p, s, m);
				else
				{
					if (rt == LUA_TFUNCTION)
					{
						lua_pushvalue(L, 3);
						PushMatch(L, *p, s, m);
						lua_call(L, 1, 1);
					}
					else
					{
						Push(L, whole);
						lua_gettable(L, 3);
					}
					if (lua_isstring(L, -1)) out += std::string(View(L, -1));
					else if (lua_isnil(L, -1) || (lua_isboolean(L, -1) && !lua_toboolean(L, -1))) out.append(whole);
					else return luaL_error(L, "invalid replacement value (a %s)", luaL_typename(L, -1));
					lua_pop(L, 1);
				}
				++count;
				last = me;
				if (me == ms)
				{
					size_t i = me;
					if (i < s.size())
					{
						const size_t before = i;
						DecodeCp(s.data(), s.size(), i);
						out.append(s.substr(before, i - before));
						last = i;
					}
					else i = s.size() + 1;
					pos = i;
				}
				else
					pos = me;
			}
			if (last < s.size()) out.append(s.substr(last));
			Push(L, out);
			lua_pushinteger(L, count);
			return 2;
		}
		int RxSplit(lua_State* L)
		{
			bool mod;
			auto p = RegexArg(L, 1, mod);
			const std::string_view s = View(L, 2);
			const lua_Integer limit = luaL_optinteger(L, 3, -1);
			lua_newtable(L);
			lua_Integer n = 0;
			size_t pos = 0, last = 0;
			while (pos <= s.size() && (limit < 0 || n + 1 < limit))
			{
				Match m;
				bool aborted = false;
				if (!FindFrom(*p, s, pos, m, aborted))
				{
					if (aborted) return luaL_error(L, "regex too complex");
					break;
				}
				const size_t ms = (size_t)m.caps[0].first, me = (size_t)m.caps[0].second;
				if (me == ms)
				{
					// empty separator: split between characters
					if (ms >= s.size()) break;
					size_t i = ms;
					if (ms == last) { DecodeCp(s.data(), s.size(), i); pos = i; if (i >= s.size()) break; Push(L, s.substr(last, i - last)); lua_rawseti(L, -2, ++n); last = i; continue; }
				}
				Push(L, s.substr(last, ms - last));
				lua_rawseti(L, -2, ++n);
				for (int g = 1; g <= p->groups; ++g)
				{
					const auto c = m.caps[(size_t)g];
					if (c.first >= 0) { Push(L, s.substr((size_t)c.first, (size_t)(c.second - c.first))); lua_rawseti(L, -2, ++n); }
				}
				last = me;
				pos = me == ms ? me + 1 : me;
			}
			Push(L, s.substr(std::min(last, s.size())));
			lua_rawseti(L, -2, ++n);
			return 1;
		}
		int RxEscape(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::string out;
			for (char c : s)
			{
				if (strchr("\\^$.|?*+()[]{}-/", c)) out += '\\';
				out += c;
			}
			Push(L, out);
			return 1;
		}
		int RxToString(lua_State* L)
		{
			Regex* r = static_cast<Regex*>(luaL_checkudata(L, 1, kRegex));
			Push(L, "/" + r->source + "/" + r->flags);
			return 1;
		}
		int RxGroups(lua_State* L)
		{
			Regex* r = static_cast<Regex*>(luaL_checkudata(L, 1, kRegex));
			lua_newtable(L);
			for (int i = 1; i <= r->prog->groups; ++i)
			{
				Push(L, r->prog->names[(size_t)i - 1]);
				lua_rawseti(L, -2, i);
			}
			return 1;
		}
	} // namespace

	int OpenRegex(lua_State* L)
	{
		static const luaL_Reg methods[] = {{"test", RxTest}, {"find", RxFind}, {"match", RxMatch}, {"find_all", RxFindAll}, {"gmatch", RxGmatch},
			{"gsub", RxGsub}, {"split", RxSplit}, {"groups", RxGroups}, {nullptr, nullptr}};
		static const luaL_Reg mm[] = {{"__tostring", RxToString}, {nullptr, nullptr}};
		MakeClass(L, kRegex, methods, mm, GcUdata<Regex>);
		static const luaL_Reg fns[] = {{"compile", RxCompile}, {"test", RxTest}, {"find", RxFind}, {"match", RxMatch}, {"find_all", RxFindAll},
			{"gmatch", RxGmatch}, {"gsub", RxGsub}, {"split", RxSplit}, {"escape", RxEscape}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
