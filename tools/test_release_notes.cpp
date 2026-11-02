// Regression test for the GitHub release page conversion. The fixture keeps the
// structure of a real release page's Box-body. From a Visual Studio x86 command
// prompt at the repository root, in a scratch output folder of your choice:
//   cl /std:c++20 /EHsc /IDev\src tools\test_release_notes.cpp Dev\src\ReleaseNotesDownloader.cpp winhttp.lib
#include "ReleaseNotesDownloader.h"

#include <cstdio>
#include <string>

namespace
{
	constexpr char kPage[] = R"html(<!DOCTYPE html>
<html><head><title>Outside</title><script>if (a<b) { document.write('<div class="Box-body">'); }</script></head>
<body><div>Outside before</div>
<div data-view-component="true" class="Box">
<div data-view-component="true" class="Box-body">      <div class="d-flex flex-md-row flex-column">
  <div class="d-flex flex-row flex-1">
    <div class="flex-1"><h1 data-view-component="true" class="tmp-mr-3 d-inline">v9.9.9</h1>
      <span><a href="/latest" class="Link d-none d-md-inline-block"><span class="Label">Latest</span></a></span>
    </div>
    <div class="tmp-mt-3 ml-2 d-md-none"><a href="/latest"><span class="Label">Latest</span></a></div>
  </div>
  <div class="d-flex tmp-mb-3"><select-panel id="x"><dialog-helper><button type="button">Compare</button>
    <dialog><h1>Choose a tag to compare</h1><div hidden><h2>Sorry</h2></div></dialog></dialog-helper></select-panel></div>
</div>
<div class="tmp-mb-3"><div class="d-flex flex-row flex-wrap">
  <div class="tmp-mr-4"><img src="a.png" alt="@owner" /><a href="/owner">owner</a>
    released this
    <relative-time datetime="2026-07-03T02:17:06Z">03 Jul 02:17</relative-time></div>
  <div class="tmp-mr-4">&middot; <a href="/compare">5 commits</a> to master</div>
  <div class="tmp-mr-4"><svg aria-label="Tag"><path d="M1 7"></path></svg><span>v9.9.9</span></div>
</div></div>
<div data-test-selector="body-content" class="markdown-body tmp-my-3"><h2>What's Changed</h2>
<ul>
<li>Fish &amp; chips
<ul>
<li>Nested line,<br>
continued "here".</li>
</ul>
</li>
<li>Second</li>
</ul>
<p>Plain &#x41;&#66;C</p>
<ol><li>One</li><li>Two</li></ol></div>
</div>
<div data-view-component="true" class="Box-footer">Assets outside</div>
</div></body></html>)html";

	constexpr char kExpected[] =
		"What's Changed\n"
		"- Fish & chips\n"
		"  - Nested line,\n"
		"    continued \"here\".\n"
		"- Second\n"
		"\n"
		"Plain ABC\n"
		"\n"
		"1. One\n"
		"2. Two";
}

int main()
{
	const std::optional<std::string> text = ExtractReleaseNotesText(kPage);
	if (!text || *text != kExpected) {
		std::printf("Conversion mismatch:\n%s\n", text ? text->c_str() : "(no text)");
		return 1;
	}
	if (ExtractReleaseNotesText("<div class=\"Box\">No body</div>")
		|| ExtractReleaseNotesText("<div class=\"Box-body\">Title only</div><div class=\"markdown-body\">Outside</div>")
		|| ExtractReleaseNotesText("<div class=\"Box-body\"><div class=\"markdown-body\"><svg>icon</svg></div></div>")) {
		std::printf("A page without notes inside Box-body must not produce text.\n");
		return 1;
	}
	if (ExtractReleaseVersion("https://github.com/o/r/releases/tag/v1.0.4") != "1.0.4"
		|| ExtractReleaseVersion("https://github.com/o/r/releases/tag/2.0") != "2.0"
		|| ExtractReleaseVersion("https://github.com/o/r/releases/tag/v2.0-beta")
		|| ExtractReleaseVersion("https://github.com/o/r/releases/tag/v2.0\";os.exit()--")
		|| ExtractReleaseVersion("https://github.com/o/r/releases")) {
		std::printf("Release version parsing is wrong.\n");
		return 1;
	}
	if (!IsNewerVersion("2.0", "1.0.4") || !IsNewerVersion("1.0.10", "1.0.9") || !IsNewerVersion("1.0.4.1", "1.0.4")
		|| IsNewerVersion("1.0.4", "1.0.4") || IsNewerVersion("1.0", "1.0.0") || IsNewerVersion("1.0.3", "1.0.4")) {
		std::printf("Version comparison is wrong.\n");
		return 1;
	}
	std::printf("Release notes conversion checks passed.\n");
	return 0;
}
