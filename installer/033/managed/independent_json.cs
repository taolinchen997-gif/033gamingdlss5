// Bounded JSON reader for Windows PowerShell 5.1 and PowerShell 7.
// No date coercion, duplicate keys, comments, extensions, IO or native calls.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
namespace Installer033 {
    public sealed class StrictJsonV1 {
        private readonly string text;
        private int pos;
        private StrictJsonV1(string input) { text = input; }
        public static object Parse(string input) {
            if (input == null || input.Length > 8 * 1024 * 1024) throw new FormatException("JSON size limit");
            var reader = new StrictJsonV1(input);
            object result = reader.Value(0);
            reader.Space();
            if (reader.pos != input.Length) throw new FormatException("Trailing JSON data");
            return result;
        }
        private void Space() { while (pos < text.Length && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) ++pos; }
        private bool Take(char value) { Space(); if (pos < text.Length && text[pos] == value) { ++pos; return true; } return false; }
        private void Need(char value) { if (!Take(value)) throw new FormatException("Expected JSON delimiter at " + pos); }
        private object Value(int depth) {
            if (depth > 48) throw new FormatException("JSON nesting limit");
            Space(); if (pos == text.Length) throw new FormatException("Missing JSON value");
            char token = text[pos];
            if (token == '{') {
                ++pos; var map = new Dictionary<string, object>(StringComparer.Ordinal);
                if (Take('}')) return map;
                do { Space(); string key = String(); Need(':'); if (map.ContainsKey(key)) throw new FormatException("Duplicate JSON key: " + key); map.Add(key, Value(depth + 1)); } while (Take(','));
                Need('}'); return map;
            }
            if (token == '[') {
                ++pos; var list = new List<object>();
                if (Take(']')) return list;
                do { if (list.Count >= 16384) throw new FormatException("JSON array limit"); list.Add(Value(depth + 1)); } while (Take(','));
                Need(']'); return list;
            }
            if (token == '"') return String();
            foreach (string literal in new [] { "true", "false", "null" }) {
                if (text.Length - pos >= literal.Length && System.String.CompareOrdinal(text, pos, literal, 0, literal.Length) == 0) {
                    pos += literal.Length; if (literal == "null") return null; return literal == "true";
                }
            }
            int start = pos;
            if (text[pos] == '-') ++pos;
            if (pos == text.Length || text[pos] < '0' || text[pos] > '9') throw new FormatException("Invalid JSON number");
            if (text[pos] == '0') ++pos; else while (pos < text.Length && text[pos] >= '0' && text[pos] <= '9') ++pos;
            if (pos < text.Length && text[pos] == '.') { ++pos; Digits(); }
            if (pos < text.Length && (text[pos] == 'e' || text[pos] == 'E')) { ++pos; if (pos < text.Length && (text[pos] == '-' || text[pos] == '+')) ++pos; Digits(); }
            string number = text.Substring(start, pos - start); long integer;
            if (Int64.TryParse(number, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out integer)) return integer;
            double fraction;
            if (!Double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out fraction) || Double.IsInfinity(fraction) || Double.IsNaN(fraction)) throw new FormatException("JSON number out of range");
            return fraction;
        }
        private void Digits() { int start = pos; while (pos < text.Length && text[pos] >= '0' && text[pos] <= '9') ++pos; if (pos == start) throw new FormatException("Missing JSON digits"); }
        private string String() {
            if (pos == text.Length || text[pos++] != '"') throw new FormatException("Expected JSON string");
            var value = new StringBuilder();
            while (pos < text.Length) {
                char c = text[pos++];
                if (c == '"') return value.ToString();
                if (c < 32) throw new FormatException("Control character in JSON string");
                if (c != '\\') { value.Append(c); continue; }
                if (pos == text.Length) break;
                c = text[pos++];
                switch (c) {
                    case '"': case '\\': case '/': value.Append(c); break;
                    case 'b': value.Append('\b'); break; case 'f': value.Append('\f'); break;
                    case 'n': value.Append('\n'); break; case 'r': value.Append('\r'); break; case 't': value.Append('\t'); break;
                    case 'u':
                        if (pos + 4 > text.Length) throw new FormatException("Truncated Unicode escape");
                        ushort code;
                        if (!UInt16.TryParse(text.Substring(pos, 4), NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out code)) throw new FormatException("Bad Unicode escape");
                        value.Append((char)code); pos += 4; break;
                    default: throw new FormatException("Unknown JSON escape");
                }
            }
            throw new FormatException("Unterminated JSON string");
        }
    }
}
