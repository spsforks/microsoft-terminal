using System.Text;
using System.Text.RegularExpressions;

string data;
using (var client = new HttpClient())
{
    var response = await client.GetAsync("https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakTest.txt");
    response.EnsureSuccessStatusCode();
    data = await response.Content.ReadAsStringAsync();
}

var testString = new StringBuilder();
var scanner = new StringReader(data);
var firstLine = true;

while (await scanner.ReadLineAsync() is { } line)
{
    var parts = line.Split('#');
    var test = parts[0].Trim();
    var comment = parts.Length > 1 ? parts[1].Trim() : "";

    if (firstLine)
    {
        firstLine = false;

        var re = new Regex(@"^GraphemeBreakTest-(\d+\.\d+\.\d+)\.txt$");
        var m = re.Match(comment);
        if (!m.Success)
        {
            throw new Exception($"Failed to find version number, got: {comment}");
        }

        testString.Append(
            $$"""
            // Generated by GraphemeTestTableGen
            // on {{DateTime.UtcNow.ToString("yyyy'-'MM'-'dd'T'HH':'mm':'ssK")}}, from Unicode {{m.Groups[1].Value}}
            struct GraphemeBreakTest
            {
                const wchar_t* comment;
                const wchar_t* graphemes[4];
            };
            static constexpr GraphemeBreakTest s_graphemeBreakTests[] = {

            """
        );
    }

    if (test == "" || comment == "")
    {
        continue;
    }

    var graphemes = test.Split('÷');
    for (var i = 0; i < graphemes.Length; i++)
    {
        graphemes[i] = graphemes[i].Trim();
    }

    testString.Append($"    {{ L\"{comment}\"");

    foreach (var g in graphemes)
    {
        if (string.IsNullOrEmpty(g))
        {
            continue;
        }

        testString.Append(", L\"");

        var codepoints = g.Split('×');
        foreach (var c in codepoints)
        {
            var i = Convert.ToUInt32(c.Trim(), 16);
            switch (i)
            {
                case 0x07:
                    testString.Append("\\a");
                    break;
                case 0x08:
                    testString.Append("\\b");
                    break;
                case 0x09:
                    testString.Append("\\t");
                    break;
                case 0x0A:
                    testString.Append("\\n");
                    break;
                case 0x0B:
                    testString.Append("\\v");
                    break;
                case 0x0C:
                    testString.Append("\\f");
                    break;
                case 0x0D:
                    testString.Append("\\r");
                    break;
                case >= 0x20 and <= 0x7e:
                    testString.Append((char)i);
                    break;
                case <= 0xff:
                    testString.Append($"\\x{i:X2}");
                    break;
                case <= 0xffff:
                    testString.Append($"\\x{i:X4}");
                    break;
                default:
                    testString.Append($"\\U{i:X8}");
                    break;
            }
        }

        testString.Append("\"");
    }

    testString.Append(" },\n");
}

testString.Append("};\n");

Console.OutputEncoding = System.Text.Encoding.UTF8;
Console.Write(testString);