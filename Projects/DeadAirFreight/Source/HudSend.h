#pragma once

#include "Engine/UI/UIRuntime.h"

#include <map>
#include <string>
#include <utility>

// Pushes json to a page only when it changed. Every push dirties the page, which then repaints
// and re-uploads its whole texture, so resending the same HUD each frame costs a full redraw.
inline void SendIfChanged(FP::UIDocumentInstance& page, const char* key, const std::string& json)
{
	// ponytail: keyed by page address, so a new page reusing a freed one's address skips an
	// unchanged first push. Clear this on page load if that ever shows.
	static std::map<std::pair<const void*, std::string>, std::string> last;
	std::string& sent = last[{ &page, key }];
	if (sent == json)
		return;
	sent = json;
	page.SetJson(key, json);
}
