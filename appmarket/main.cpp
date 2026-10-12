// R AppMarket -- a thin browser/installer GUI for a set of Haiku package
// repositories, filtered to whichever ones support the architecture this
// binary was actually built for (see kRepos/CurrentArch below). It does
// not reimplement dependency resolution or download/verification: it lists
// what each matching repo's packages.txt advertises and shells out to the
// already-trusted `pkgman` and `curl` for everything that touches the
// network or the package database, the same tools a user would run by
// hand in Terminal.
#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <OS.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Strings.h"
using namespace rappmarket;

// Stands in for BObjectList<T, Owning> -- RENKU arm64 is built against a
// Haiku nightly new enough that BObjectList takes ownership as a second
// template argument, while the official R1 Beta 4 headers the x86_64/x86
// cross-builds use still take it as a runtime constructor bool instead (an
// upstream Haiku API change this app predates on those architectures).
// Rather than special-case two incompatible BObjectList signatures, this
// covers the handful of operations the file actually needs (add, index,
// count, remove-without-deleting, clear, sort) over a plain std::vector, and
// compiles identically everywhere.
template<class T, bool Owning = false>
class PtrList {
public:
	PtrList(int32 reserve = 8) { fItems.reserve(reserve); }
	~PtrList() { MakeEmpty(); }

	int32 CountItems() const { return (int32)fItems.size(); }
	T* ItemAt(int32 i) const { return fItems[i]; }
	void AddItem(T* item) { fItems.push_back(item); }

	// Always just detaches, whether or not this list owns its items -- the
	// same as BObjectList's own RemoveItemAt, so a caller that wants the
	// detached item deleted (or kept) does that itself either way.
	T* RemoveItemAt(int32 i)
	{
		T* item = fItems[i];
		fItems.erase(fItems.begin() + i);
		return item;
	}

	void MakeEmpty()
	{
		if (Owning) {
			for (size_t i = 0; i < fItems.size(); i++)
				delete fItems[i];
		}
		fItems.clear();
	}

	void SortItems(int (*compare)(const T*, const T*))
	{
		std::sort(fItems.begin(), fItems.end(),
			[compare](T* a, T* b) { return compare(a, b) < 0; });
	}

private:
	std::vector<T*> fItems;
};

static const char* kPackagesDir = "/boot/system/packages";

// Every repo R AppMarket knows about by default, each tagged with the one
// architecture it actually ships packages for. Haiku package repositories
// are architecture-specific (a pkgman repo URL serves one arch's hpkgs,
// never a mix), so there is no per-package arch field to filter on --
// filtering happens per-repo, against whatever architecture this binary
// was itself built for (CurrentArch below). A repo whose arch doesn't
// match is skipped entirely: never listed, never added via `pkgman
// add-repo`, never fetched.
//
// No separate display-name field: the repo's own hostname (see
// HostFromURL below) is what's shown, so the displayed tag can never
// drift from the URL that's actually being queried.
struct RepoDef {
	const char* url;
	const char* arch; // "arm64", "x86_64", or "x86"
};

static const RepoDef kRepos[] = {
	{ "https://pkgman.rainygirl.com/arm64", "arm64" },
	{ "https://spritzdepot.desktoponfire.com/repo/x86_64/current", "x86_64" },
	{ "https://kiri.sen-labs.org/x86_64", "x86_64" },
	{ "https://software.besly.de/repo64", "x86_64" },
	{ "http://fatelk.com/repo_64", "x86_64" },
};
static const int32 kRepoCount = sizeof(kRepos) / sizeof(kRepos[0]);

// Whether `url` is one of the compiled-in repos above -- used to keep a
// user from "adding" a repo that's already built in (a harmless no-op,
// but silently doing nothing is less confusing than silently adding a
// duplicate), and to keep the Remove Repo menu limited to repos the user
// actually added themselves (see _RebuildRemoveRepoMenu): a built-in
// repo was never meant to be removable at runtime.
static bool
IsBuiltinRepoURL(const BString& url)
{
	for (int32 i = 0; i < kRepoCount; i++) {
		if (url.ICompare(kRepos[i].url) == 0)
			return true;
	}
	return false;
}

// Just the host part of a repo URL ("pkgman.rainygirl.com" out of
// "https://pkgman.rainygirl.com/arm64") -- what gets shown as the Repo
// column/filter tag.
static BString
HostFromURL(const char* url)
{
	const char* host = strstr(url, "://");
	host = host != NULL ? host + 3 : url;
	const char* end = host;
	while (*end != '\0' && *end != '/' && *end != ':')
		end++;
	return BString(host, (int32)(end - host));
}

// Haiku has no fat/universal binaries -- a given build of this app only
// ever runs on the architecture it was compiled for -- so the running
// architecture is known at compile time, not something to detect at
// runtime via uname(). Only the arm64 side of this is actually verified
// (see AGENTS.md); the x86_64/x86 branches exist so a future x86 build of
// this same source shows only repos that can actually serve it.
static const char*
CurrentArch()
{
#if defined(__aarch64__)
	return "arm64";
#elif defined(__x86_64__)
	return "x86_64";
#elif defined(__i386__)
	return "x86";
#else
#error "R AppMarket: unrecognized target architecture"
#endif
}

static const uint32 kMsgRefreshDone = 'RFsh';
static const uint32 kMsgOpDone = 'ROpD';
static const uint32 kMsgAction = 'RAct';
static const uint32 kMsgSelectionChanged = 'RSel';
static const uint32 kMsgFilterChanged = 'RFlt';
static const uint32 kMsgSortChanged = 'RSrt';
static const uint32 kMsgSearchChanged = 'RSch';
static const uint32 kMsgIconDone = 'RIcn';
static const uint32 kMsgShowAddRepo = 'RAdS';
static const uint32 kMsgAddRepo = 'RAdd';
static const uint32 kMsgRemoveRepo = 'RRmv';
static const uint32 kMsgInstallLog = 'RInL';

// Above this, the icon prefetch skips downloading a package's .hpkg at
// all -- the point is a quick icon, not a way to pre-fetch large
// packages. Checked via a HEAD request before the real download.
static const off_t kMaxIconFetchSize = 10 * 1024 * 1024;

// Every package's icon is fetched in the background right after a
// refresh (see _QueueIconFetches), not just the selected one -- but all
// 28-ish of them at once reproduced the exact pile-up that used to
// starve a single selected fetch (see the old debounce this replaced).
// Capping how many of the real .hpkg downloads run concurrently keeps
// the rest queued instead of all competing at once.
static const int32 kMaxConcurrentIconFetches = 3;

// Upper bound on how many rows _RebuildVisibleList() ever hands to
// fListView at once -- see its own comment for why.
static const int32 kMaxVisiblePackages = 200;

enum SortMode { kSortByPackage, kSortByRepoName };

// Runs a shell command and returns its stdout, trimmed of a trailing
// newline. Used only for the fixed, non-user-controlled commands below --
// never with package names or other external input interpolated unquoted.
static BString
RunCaptured(const char* command)
{
	BString result;
	FILE* pipe = popen(command, "r");
	if (pipe == NULL)
		return result;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0)
		result.Append(buf, (int32)n);
	pclose(pipe);
	return result;
}

static bool
IsPackageInstalled(const BString& name)
{
	BDirectory dir(kPackagesDir);
	if (dir.InitCheck() != B_OK)
		return false;

	BString prefix(name);
	prefix << "-";

	BEntry entry;
	char entryName[B_FILE_NAME_LENGTH];
	dir.Rewind();
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.GetName(entryName) != B_OK)
			continue;
		if (strncmp(entryName, prefix.String(), prefix.Length()) == 0)
			return true;
	}
	return false;
}

// `pkgman add-repo` on a URL it already has registered asks an interactive
// "overwrite? [yes/no]" question instead of just succeeding -- with no tty
// attached (this runs via popen()), that prompt reads EOF forever and the
// process never exits, hanging this whole refresh. `pkgman list-repos`
// prints each repo's base-url, so checking that first avoids ever calling
// add-repo on a URL it already knows about.
static bool
IsRepoKnown(const char* url)
{
	BString listing = RunCaptured("pkgman list-repos 2>/dev/null");
	return listing.FindFirst(url) >= 0;
}

// How wide the "Repo" column is drawn, in pixels. Package names never
// overlap it, regardless of how the BListView itself is resized -- this
// is a fixed two-column layout, not something that reflows.
static const float kRepoColumnWidth = 280.0f;

// The actual resolution a package's icon is fetched at (see
// IconThreadFunc) -- shared between the detail panel's own preview and
// each row's smaller copy of the same bitmap. A row never draws its icon
// larger than this: scaling a 48x48 bitmap down to a small row icon
// looks fine, scaling it up past its native resolution would just blur.
static const float kIconPreviewSize = 48.0f;

// A two-line row's height depends on the view's actual font -- there's
// no single fixed pixel count that works both at a normal desktop font
// size and at the much larger font this port's own HiDPI test guest
// turned out to be running (24pt, not whatever ordinary desktop size was
// assumed here first): a hardcoded height clipped the second line
// (shortdesc) off the bottom of every row, under a font that size, with
// nothing visibly wrong except the description simply never appearing.
// Computed once here from font_height so Update() (which sets the row's
// actual height) and DrawItem() (which lays out both lines inside it)
// never disagree about how tall a row needs to be.
static float
ComputeRowHeight(const font_height& fh)
{
	float nameLine = ceilf(fh.ascent + fh.descent + fh.leading);
	float descLine = ceilf((fh.ascent + fh.descent) * 0.85f);
	return 10.0f + nameLine + 3.0f + descLine;
}

// A two-line row: an icon, the package name (plus an "(installed)"
// suffix) on the first line, and a truncated one-line blurb (see
// PackageItem::fShortDesc) on the second -- plus the repo name in its
// own column, vertically centered against the full two-line height, with
// a vertical rule between them. BStringItem can't do any of this -- it
// is exactly one string -- so this draws itself directly instead of just
// holding display text.
//
// An earlier version had no per-row icon at all: a prior placeholder
// drew this app's own icon on every row, which just read as "the same
// fake icon repeated 28 times" -- worse than none. Real per-package
// icons only became practical once they were fetched proactively for
// the whole list right after a refresh (_QueueIconFetches) instead of
// one at a time, lazily, per selection.
class PackageItem : public BListItem {
public:
	PackageItem(const BString& name, const BString& repo,
		const BString& repoUrl, bool installed, const BString& shortDesc,
		const BString& fullDesc)
		:
		BListItem(),
		fName(name),
		fRepo(repo),
		fRepoUrl(repoUrl),
		fShortDesc(shortDesc),
		fFullDesc(fullDesc),
		fInstalled(installed),
		fIcon(NULL),
		fBusy(false),
		fBusyInstall(false),
		fButtonRect(),
		fHovered(false)
	{
	}

	~PackageItem()
	{
		delete fIcon;
	}

	const BString& PackageName() const { return fName; }
	const BString& Repo() const { return fRepo; }
	const BString& RepoUrl() const { return fRepoUrl; }
	const BString& ShortDescription() const { return fShortDesc; }
	const BString& FullDescription() const { return fFullDesc; }
	bool Installed() const { return fInstalled; }
	void SetInstalled(bool installed) { fInstalled = installed; }

	// Owns whatever bitmap it's handed (deletes the previous one first).
	BBitmap* Icon() const { return fIcon; }
	void SetIcon(BBitmap* icon)
	{
		delete fIcon;
		fIcon = icon;
	}

	// Set while this row's own install/uninstall job is in flight (see
	// AppMarketWindow::_StartInstallJob/_HandleOpDone) -- shows a "..."
	// label on this row's button instead of Install/Uninstall and makes it
	// unclickable. sButtonsEnabled is separate and global: pkgman isn't
	// safe to run twice concurrently against the same package database,
	// so every row's button is disabled while *any* row has a job running,
	// not just the one that started it.
	bool IsBusy() const { return fBusy; }
	void SetBusy(bool busy, bool install)
	{
		fBusy = busy;
		fBusyInstall = install;
	}
	bool IsButtonClickable() const { return sButtonsEnabled && !fBusy; }

	// Whether the pointer is currently over this row's button -- see
	// PackageListView::MouseMoved, the only thing that sets this.
	bool IsHovered() const { return fHovered; }
	void SetHovered(bool hovered) { fHovered = hovered; }

	// Recomputed on every DrawItem() -- a row has to have been drawn at
	// least once (true by the time a user could have clicked it) for this
	// to be meaningful. Lets PackageListView::MouseDown hit-test the exact
	// same rect DrawItem just painted, without duplicating its font-metric
	// math.
	BRect ButtonRect() const { return fButtonRect; }

	static bool sButtonsEnabled;

	void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		SetHeight(ComputeRowHeight(fh));
	}

	void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetLowColor(background);
		owner->SetHighColor(background);
		owner->FillRect(frame);

		rgb_color textColor = IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR);

		BFont font;
		owner->GetFont(&font);
		font_height fh;
		font.GetHeight(&fh);

		// Shared by the repo column and the description line below the
		// name -- both are secondary text, drawn smaller than the name.
		BFont smallFont(font);
		smallFont.SetSize(font.Size() * 0.85f);
		font_height sfh;
		smallFont.GetHeight(&sfh);

		// The name is the one thing on a row meant to stand out, so it's
		// drawn bold; everything else (repo, description) stays regular
		// weight at or below the base size.
		BFont boldFont(font);
		boldFont.SetFace(B_BOLD_FACE);
		font_height bdfh;
		boldFont.GetHeight(&bdfh);

		// Repo column, vertically centered against the full (two-line)
		// row height -- truncated with an ellipsis rather than widening
		// the column to fit, since the tag is now a full hostname (see
		// HostFromURL) and an unusually long one should clip instead of
		// pushing into the package column.
		owner->SetFont(&smallFont);
		owner->SetHighColor(textColor);
		owner->SetLowColor(background);
		BString repoText(fRepo);
		owner->TruncateString(&repoText, B_TRUNCATE_END,
			kRepoColumnWidth - 8);
		float repoBaseline = frame.top
			+ (frame.Height() - (sfh.ascent + sfh.descent)) / 2.0f + sfh.ascent;
		owner->DrawString(repoText.String(), BPoint(frame.left + 6, repoBaseline));
		owner->SetFont(&font);

		rgb_color ruleColor = tint_color(background, B_DARKEN_1_TINT);
		owner->SetHighColor(ruleColor);
		owner->StrokeLine(
			BPoint(frame.left + kRepoColumnWidth, frame.top),
			BPoint(frame.left + kRepoColumnWidth, frame.bottom));

		// Capped at the icon's own native resolution (kIconPreviewSize) --
		// past that it would just be upscaled and blurry, not sharper.
		float iconSize = frame.Height() - 8.0f;
		if (iconSize > kIconPreviewSize)
			iconSize = kIconPreviewSize;

		float iconX = frame.left + kRepoColumnWidth + 8;
		if (fIcon != NULL) {
			float iconY = frame.top + (frame.Height() - iconSize) / 2.0f;
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(fIcon, BRect(iconX, iconY,
				iconX + iconSize - 1, iconY + iconSize - 1));
			owner->SetDrawingMode(B_OP_COPY);
		}

		// The Install/Uninstall button sits on the right edge of the row --
		// measured here, before maxTextWidth below, so the name/description
		// text truncates to leave room for it instead of running underneath.
		// Sized from the longer of "Install"/"Uninstall" (not whichever one
		// this row happens to need right now) so the button's width -- and
		// therefore how much text space is reserved for it -- never changes
		// between an install and its own eventual uninstall; drawn in the
		// same smallFont as the repo/description text for consistency.
		BString installLabel(T(kInstall));
		BString uninstallLabel(T(kUninstall));
		owner->SetFont(&smallFont);
		float buttonTextWidth = std::max(owner->StringWidth(installLabel.String()),
			owner->StringWidth(uninstallLabel.String()));
		owner->SetFont(&font);

		float buttonHeight = ceilf(sfh.ascent + sfh.descent) + 10.0f;
		float buttonWidth = buttonTextWidth + 32.0f;
		if (buttonWidth < 70.0f)
			buttonWidth = 70.0f;

		float buttonRight = frame.right - 10.0f;
		float buttonTop = frame.top + (frame.Height() - buttonHeight) / 2.0f;
		fButtonRect = BRect(buttonRight - buttonWidth, buttonTop, buttonRight,
			buttonTop + buttonHeight);

		float textX = iconX + kIconPreviewSize + 8;
		float maxTextWidth = frame.right - textX - 6 - (buttonWidth + 14.0f);

		// The "installed" badge is a small rounded pill drawn after the
		// name, not appended as "(installed)" text -- measured first (in
		// its own smaller font) so the name can be truncated to leave
		// room for it rather than the two overlapping.
		BString badgeLabel;
		float badgeWidth = 0.0f, badgeHeight = 0.0f;
		BFont badgeFont(font);
		font_height bfh;
		if (fInstalled) {
			badgeFont.SetSize(font.Size() * 0.7f);
			badgeFont.GetHeight(&bfh);
			badgeLabel = T(kInstalled);
			owner->SetFont(&badgeFont);
			badgeWidth = owner->StringWidth(badgeLabel.String()) + 16.0f;
			badgeHeight = ceilf(bfh.ascent + bfh.descent) + 8.0f;
			owner->SetFont(&font);
		}

		owner->SetFont(&boldFont);
		owner->SetHighColor(textColor);
		owner->SetLowColor(background);
		BString nameText(fName);
		owner->TruncateString(&nameText, B_TRUNCATE_END,
			maxTextWidth - (fInstalled ? badgeWidth + 8.0f : 0.0f));
		float nameBaseline = frame.top + 5.0f + bdfh.ascent;
		owner->DrawString(nameText.String(), BPoint(textX, nameBaseline));
		owner->SetFont(&font);

		if (fInstalled) {
			static const rgb_color kBadgeBackground = { 76, 175, 80, 255 };
			static const rgb_color kBadgeTextColor = { 255, 255, 255, 255 };

			float nameLineCenter = frame.top + 5.0f + (bdfh.ascent + bdfh.descent) / 2.0f;
			float badgeLeft = textX + boldFont.StringWidth(nameText.String()) + 8.0f;
			float badgeTop = nameLineCenter - badgeHeight / 2.0f;
			BRect badgeRect(badgeLeft, badgeTop,
				badgeLeft + badgeWidth, badgeTop + badgeHeight);

			owner->SetHighColor(kBadgeBackground);
			owner->FillRoundRect(badgeRect, badgeHeight / 2.0f, badgeHeight / 2.0f);

			owner->SetFont(&badgeFont);
			owner->SetHighColor(kBadgeTextColor);
			owner->SetLowColor(kBadgeBackground);
			float badgeBaseline = badgeTop
				+ (badgeHeight - (bfh.ascent + bfh.descent)) / 2.0f + bfh.ascent;
			owner->DrawString(badgeLabel.String(), BPoint(badgeLeft + 8.0f, badgeBaseline));
			owner->SetFont(&font);
			owner->SetLowColor(background);
		}

		if (fShortDesc.Length() > 0) {
			owner->SetFont(&smallFont);

			// A muted version of the text color, blended toward the
			// background rather than just darkened, so it stays legible
			// whether the row is selected or not.
			rgb_color descColor;
			descColor.red = (uint8)((textColor.red + background.red) / 2);
			descColor.green = (uint8)((textColor.green + background.green) / 2);
			descColor.blue = (uint8)((textColor.blue + background.blue) / 2);
			descColor.alpha = 255;
			owner->SetHighColor(descColor);
			owner->SetLowColor(background);

			BString descText(fShortDesc);
			owner->TruncateString(&descText, B_TRUNCATE_END, maxTextWidth);
			float descBaseline = nameBaseline + bdfh.descent + 3.0f + sfh.ascent;
			owner->DrawString(descText.String(), BPoint(textX, descBaseline));

			owner->SetFont(&font);
		}

		// The button itself, in the slot reserved for it above. Red for
		// Uninstall, blue for Install, matching no particular Haiku
		// convention -- there isn't one for this -- just picked to read as
		// "stop"/"go" at a glance. Greyed out and unclickable (see
		// IsButtonClickable) either for this one specific row, while its
		// own job is running (shows "Installing..."/"Removing..." instead
		// of its usual label), or for every row at once while some other
		// row's job is running -- pkgman isn't safe to invoke twice
		// concurrently against the same package database.
		BString buttonLabel = fBusy
			? BString(T(fBusyInstall ? kInstallingShort : kRemovingShort))
			: (fInstalled ? uninstallLabel : installLabel);

		owner->SetFont(&smallFont);
		float buttonLabelWidth = owner->StringWidth(buttonLabel.String());

		bool clickable = sButtonsEnabled && !fBusy;
		rgb_color buttonFill, buttonTextColor;
		if (!clickable) {
			buttonFill = tint_color(background, B_DARKEN_2_TINT);
			buttonTextColor = tint_color(background, B_DARKEN_4_TINT);
		} else if (fInstalled) {
			// Uninstall stays a neutral grey until the pointer is actually
			// over it (see PackageListView::MouseMoved) -- red only shows
			// up right as the user is about to click the destructive
			// action, not just because the row happens to be installed.
			buttonFill = fHovered
				? (rgb_color){ 198, 40, 40, 255 }
				: (rgb_color){ 158, 158, 158, 255 };
			buttonTextColor = (rgb_color){ 255, 255, 255, 255 };
		} else {
			buttonFill = (rgb_color){ 33, 150, 243, 255 };
			buttonTextColor = (rgb_color){ 255, 255, 255, 255 };
		}

		owner->SetHighColor(buttonFill);
		owner->FillRoundRect(fButtonRect, 4.0f, 4.0f);

		owner->SetHighColor(buttonTextColor);
		owner->SetLowColor(buttonFill);
		float buttonBaseline = fButtonRect.top
			+ (fButtonRect.Height() - (sfh.ascent + sfh.descent)) / 2.0f + sfh.ascent;
		float buttonTextX = fButtonRect.left
			+ (fButtonRect.Width() - buttonLabelWidth) / 2.0f;
		owner->DrawString(buttonLabel.String(), BPoint(buttonTextX, buttonBaseline));
		owner->SetFont(&font);
		owner->SetLowColor(background);
	}

private:
	BString fName;
	BString fRepo;
	BString fRepoUrl;
	BString fShortDesc;
	BString fFullDesc;
	bool fInstalled;
	BBitmap* fIcon;
	bool fBusy;
	bool fBusyInstall;
	BRect fButtonRect;
	bool fHovered;
};

bool PackageItem::sButtonsEnabled = true;

// A plain BListView has no way to put a real, separately-clickable child
// control inside each row -- BListItem only draws, it isn't a view -- so
// the per-row Install/Uninstall button (see PackageItem::DrawItem) is hit-
// tested here by hand: a click lands on the button instead of the normal
// select-this-row behavior only when it falls inside the rect DrawItem
// itself just painted. Posting straight to Window() rather than going
// through a stored BMessenger keeps this in step with how AddRepoWindow's
// own buttons already talk back to the main window, elsewhere in this file.
class PackageListView : public BListView {
public:
	PackageListView(const char* name)
		:
		BListView(name, B_SINGLE_SELECTION_LIST),
		fHoverIndex(-1)
	{
		// Every row's own drawing (text truncation, the button's position)
		// depends on the row's actual width, not just its height -- but a
		// plain BView, on a resize, only asks Draw() to repaint the newly
		// exposed strip, not the rows that were already visible. Widening
		// the window left every already-drawn row showing its old,
		// narrower-frame content underneath the new blank strip rather
		// than redrawing to match (looked like the list tearing itself
		// apart, not an app_server/graphics-driver bug). This flag makes
		// a resize invalidate the whole view instead of just the delta.
		SetFlags(Flags() | B_FULL_UPDATE_ON_RESIZE);
	}

	void MouseDown(BPoint where)
	{
		int32 index = IndexOf(where);
		PackageItem* item = index >= 0 ? (PackageItem*)ItemAt(index) : NULL;
		if (item != NULL && item->ButtonRect().Contains(where)
				&& item->IsButtonClickable()) {
			if (CurrentSelection() != index)
				Select(index);
			BMessage action(kMsgAction);
			action.AddInt32("index", index);
			Window()->PostMessage(&action);
			return;
		}
		BListView::MouseDown(where);
	}

	// Hovering the Uninstall button is the only thing that turns it red
	// (see PackageItem::DrawItem) -- everywhere else it's a neutral grey,
	// so the destructive color only shows up right as the user is about
	// to click it. Tracked per-row on PackageItem itself (not here)
	// since DrawItem is what actually reads it.
	void MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
	{
		int32 index = code != B_EXITED_VIEW ? IndexOf(where) : -1;
		PackageItem* item = index >= 0 ? (PackageItem*)ItemAt(index) : NULL;
		bool hovered = item != NULL && item->ButtonRect().Contains(where);

		if (index != fHoverIndex) {
			if (fHoverIndex >= 0) {
				PackageItem* prev = (PackageItem*)ItemAt(fHoverIndex);
				if (prev != NULL && prev->IsHovered()) {
					prev->SetHovered(false);
					InvalidateItem(fHoverIndex);
				}
			}
			fHoverIndex = index;
		}
		if (item != NULL && item->IsHovered() != hovered) {
			item->SetHovered(hovered);
			InvalidateItem(index);
		}

		BListView::MouseMoved(where, code, dragMessage);
	}

private:
	int32 fHoverIndex;
};

struct InstallJob {
	BMessenger target;
	BString name;
	bool install; // true = install, false = uninstall
};

// Repos the user has added beyond the built-in kRepos table (see
// AddRepoWindow/kMsgAddRepo) are persisted here as a flattened BMessage
// of "url" strings, one per custom repo -- kRepos itself is a compiled-in
// const array and was never meant to be edited at runtime, so user
// additions live entirely in this separate file instead. There is
// deliberately no equivalent "removed" list: a built-in repo can't be
// removed at all (see _RebuildRemoveRepoMenu), only custom ones can.
static BString
CustomReposSettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BString();
	path.Append("RAppMarket_repos");
	return BString(path.Path());
}

static void
LoadCustomRepos(PtrList<BString, true>* repos)
{
	BString path = CustomReposSettingsPath();
	if (path.Length() == 0)
		return;

	BFile file(path.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage stored;
	if (stored.Unflatten(&file) != B_OK)
		return;

	BString url;
	for (int32 i = 0; stored.FindString("url", i, &url) == B_OK; i++)
		repos->AddItem(new BString(url));
}

static void
SaveCustomRepos(const PtrList<BString, true>& repos)
{
	BString path = CustomReposSettingsPath();
	if (path.Length() == 0)
		return;

	BMessage stored;
	for (int32 i = 0; i < repos.CountItems(); i++)
		stored.AddString("url", *repos.ItemAt(i));

	BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		stored.Flatten(&file);
}

struct RefreshJob {
	BMessenger target;
	// Owned by the job; deleted by RefreshThreadFunc once it's done with
	// it. Built by _Refresh() from kRepos (filtered to the current arch)
	// plus whatever custom repos the user has added, so the thread itself
	// never has to know about either source.
	PtrList<BString, true>* repoUrls;
};

// `pkgman info` reads straight out of the local repo index this app
// already fetched via add-repo/refresh -- no network round-trip, so this
// runs once per package synchronously inside RefreshThreadFunc (already a
// background thread) instead of needing its own thread per call. Pulls
// out both the one-line blurb a row shows under its name and the full
// text the detail panel shows on selection, so neither one ever makes
// the user wait on a fetch triggered by clicking something -- both are
// already sitting in the just-refreshed PackageItem.
static void
FetchPackageInfo(const BString& name, BString* shortDesc, BString* fullDesc)
{
	BString cmd("pkgman info '");
	BString escaped(name);
	escaped.ReplaceAll("'", "'\\''");
	cmd << escaped << "' 2>/dev/null";

	BString output = RunCaptured(cmd.String());
	output.Trim();
	*fullDesc = output;

	int32 nl = output.FindFirst('\n');
	BString firstLine = nl >= 0 ? BString(output.String(), nl) : output;
	firstLine.Trim();

	// pkgman always prefixes the short line with "<name>: " -- redundant
	// once it's sitting right under the name itself on the row above it.
	BString prefix(name);
	prefix << ":";
	if (firstLine.IFindFirst(prefix) == 0) {
		firstLine.Remove(0, prefix.Length());
		firstLine.Trim();
	}
	*shortDesc = firstLine;
}

// Finds `name`'s current .hpkg filename in a repo by scraping its plain
// Apache-style `packages/` directory listing (`<a href="...">`) -- the
// repo index pkgman itself uses for `add-repo`/`info` has no subcommand
// to just print a resolved download URL, and old versions of a package
// can still be listed there alongside the current one, so this picks
// whichever matching entry has the highest `-<revision>-` number.
static BString
ResolveHpkgFilename(const BString& repoUrl, const BString& name)
{
	BString listCmd("curl -fsS '");
	listCmd << repoUrl << "/packages/' 2>/dev/null";
	BString listing = RunCaptured(listCmd.String());
	if (listing.Length() == 0)
		return BString();

	BString prefix(name);
	prefix << "-";
	BString suffix("-");
	suffix << CurrentArch() << ".hpkg";

	BString best;
	int32 bestRevision = -1;
	int32 pos = 0;
	while (true) {
		int32 hrefPos = listing.FindFirst("href=\"", pos);
		if (hrefPos < 0)
			break;
		int32 start = hrefPos + 6;
		int32 end = listing.FindFirst("\"", start);
		if (end < 0)
			break;

		BString candidate;
		listing.CopyInto(candidate, start, end - start);
		pos = end + 1;

		if (candidate.FindFirst(prefix) == 0
				&& candidate.FindLast(suffix)
					== candidate.Length() - suffix.Length()) {
			BString middle;
			candidate.CopyInto(middle, prefix.Length(),
				candidate.Length() - prefix.Length() - suffix.Length());
			int32 lastDash = middle.FindLast('-');
			BString revisionStr = lastDash >= 0
				? BString(middle.String() + lastDash + 1) : middle;
			int32 revision = atoi(revisionStr.String());
			if (revision > bestRevision) {
				bestRevision = revision;
				best = candidate;
			}
		}
	}

	return best;
}

// `package list -p` prints one bare path per entry, with no marker for
// which ones are directories -- some packages install their main binary
// straight under apps/ (apps/RPainter), others put it inside its own
// folder alongside bundled shared libs (apps/R SDR/R SDR, next to
// apps/R SDR/lib/*.so), and the folder and binary names don't always
// even match (apps/RChromium/R Chromium). So: collect every apps/...
// entry, drop whichever ones turn out to be directories (something else
// listed starts with "<path>/"), drop anything sitting inside a
// lib/fonts/locales subfolder or whose own filename has an extension
// (shared libs and data files, not a bare Haiku executable), and take
// whichever surviving candidate is shallowest -- the main binary is
// never buried deeper than one folder in anything this repo ships.
// Plain CLI tools (curl, htop, gzip, ...) have no apps/ entry at all and
// simply get no icon -- correct, not a bug, since Haiku's own
// generic-executable icon is what they would show for them too.
static BString
FindMainBinaryPath(const BString& listOutput)
{
	PtrList<BString, true> appsPaths(16);
	const char* p = listOutput.String();
	while (*p != '\0') {
		const char* nl = strchr(p, '\n');
		size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
		BString line(p, (int32)len);
		line.Trim();
		if (line.FindFirst("apps/") == 0 && line.Length() > 5)
			appsPaths.AddItem(new BString(line));
		if (nl == NULL)
			break;
		p = nl + 1;
	}

	BString best;
	int32 bestDepth = 1000;
	for (int32 i = 0; i < appsPaths.CountItems(); i++) {
		const BString& path = *appsPaths.ItemAt(i);

		bool isDirectory = false;
		BString childPrefix(path);
		childPrefix << "/";
		for (int32 j = 0; j < appsPaths.CountItems(); j++) {
			if (i != j && appsPaths.ItemAt(j)->FindFirst(childPrefix) == 0) {
				isDirectory = true;
				break;
			}
		}
		if (isDirectory)
			continue;

		if (path.IFindFirst("/lib/") >= 0 || path.IFindFirst("/fonts/") >= 0
				|| path.IFindFirst("/locales/") >= 0)
			continue;

		BString leaf(path);
		int32 lastSlash = leaf.FindLast('/');
		if (lastSlash >= 0)
			leaf.Remove(0, lastSlash + 1);
		if (leaf.FindFirst('.') >= 0)
			continue;

		int32 depth = 0;
		for (int32 k = 0; k < path.Length(); k++) {
			if (path[k] == '/')
				depth++;
		}
		if (depth < bestDepth) {
			bestDepth = depth;
			best = path;
		}
	}

	return best;
}

struct IconJob {
	BMessenger target;
	BString name;
	BString repoUrl;
	int32 generation;
};

// Downloads the package's own .hpkg (skipped above kMaxIconFetchSize),
// extracts just its main apps/<Name> entry, and reads that file's own
// BEOS:ICON the same way any installed app's icon is read -- no separate
// icon metadata exists anywhere in the repo index pkgman already has, so
// the only place a real per-package icon can come from is the package
// itself.
static int32
IconThreadFunc(void* data)
{
	IconJob* job = (IconJob*)data;
	BBitmap* icon = NULL;

	BString hpkgName = ResolveHpkgFilename(job->repoUrl, job->name);
	if (hpkgName.Length() > 0) {
		BString hpkgUrl(job->repoUrl);
		hpkgUrl << "/packages/" << hpkgName;

		BString sizeCmd("curl -fsSI '");
		sizeCmd << hpkgUrl << "' 2>/dev/null | grep -i '^content-length:'";
		BString sizeOutput = RunCaptured(sizeCmd.String());
		off_t size = 0;
		int32 colon = sizeOutput.FindFirst(':');
		if (colon >= 0)
			size = atoll(sizeOutput.String() + colon + 1);

		if (size > 0 && size <= kMaxIconFetchSize) {
			BString tmpDir = RunCaptured("mktemp -d");
			tmpDir.Trim();
			if (tmpDir.Length() > 0) {
				BString hpkgPath(tmpDir);
				hpkgPath << "/pkg.hpkg";

				BString downloadCmd("curl -fsS -o '");
				downloadCmd << hpkgPath << "' '" << hpkgUrl << "' 2>/dev/null";
				RunCaptured(downloadCmd.String());

				BString listCmd("package list -p '");
				listCmd << hpkgPath << "' 2>/dev/null";
				BString binaryPath = FindMainBinaryPath(RunCaptured(listCmd.String()));

				if (binaryPath.Length() > 0) {
					BString extractCmd("package extract -C '");
					extractCmd << tmpDir << "' '" << hpkgPath << "' '"
						<< binaryPath << "' 2>/dev/null";
					RunCaptured(extractCmd.String());

					BString extractedPath(tmpDir);
					extractedPath << "/" << binaryPath;

					BNode node(extractedPath.String());
					if (node.InitCheck() == B_OK) {
						BNodeInfo nodeInfo(&node);
						BBitmap* bitmap = new BBitmap(BRect(0, 0,
							kIconPreviewSize - 1, kIconPreviewSize - 1), B_RGBA32);
						if (nodeInfo.GetIcon(bitmap, B_LARGE_ICON) == B_OK)
							icon = bitmap;
						else
							delete bitmap;
					}
				}

				BString rmCmd("rm -rf '");
				rmCmd << tmpDir << "' 2>/dev/null";
				RunCaptured(rmCmd.String());
			}
		}
	}

	BMessage done(kMsgIconDone);
	done.AddString("name", job->name);
	done.AddString("repourl", job->repoUrl);
	done.AddInt32("generation", job->generation);
	if (icon != NULL)
		done.AddPointer("icon", icon);
	job->target.SendMessage(&done);

	delete job;
	return 0;
}

// pkgman's own download/checksum progress ("foo.hpkg [ KiB]") redraws one
// line with '\r' and is empty of any real number once it's not writing to
// an actual terminal (confirmed by capturing a real pkgman install over a
// plain pipe: the brackets stay blank) -- so there is no byte-level
// percentage to show for that step, from pkgman or from R InstallWizard
// either (it shows the same step as an indeterminate "Installing..." --
// see its Jobs.cpp). What IS real and worth showing live is pkgman's own
// stage-by-step messages (checksum validation, "Applying changes",
// "Cleaning up", "Done") -- this is exactly R InstallWizard's own
// RunCommand/IsWorthLogging filtering (see its PackageTools.cpp): drop
// \r-redrawn progress noise and ANSI escapes, keep only settled lines
// that have an actual letter in them.
static bool
IsWorthLoggingLine(const BString& line)
{
	if (line.Length() == 0)
		return false;
	if (line[line.Length() - 1] == ']' && line.FindLast(" [") >= 0)
		return false;
	for (int32 i = 0; i < line.Length(); i++) {
		if (isalpha((unsigned char)line[i]))
			return true;
	}
	return false;
}

// Like RunCaptured, but sends each settled line (see IsWorthLoggingLine)
// to `target` as a kMsgInstallLog as it arrives, instead of only
// returning the full output once the command has already finished -- the
// live log R AppMarket's own Install button shows, matching R
// InstallWizard's install log instead of a single static status line
// shown only after the fact.
static BString
RunCapturedStreaming(const char* command, const BMessenger& target)
{
	BString result;
	FILE* pipe = popen(command, "r");
	if (pipe == NULL)
		return result;

	BString line;
	int c;
	while ((c = fgetc(pipe)) != EOF) {
		if (c == '\r') {
			line = "";
		} else if (c == '\n') {
			result << line << "\n";
			BString trimmed(line);
			trimmed.Trim();
			if (IsWorthLoggingLine(trimmed)) {
				BMessage log(kMsgInstallLog);
				log.AddString("text", trimmed);
				target.SendMessage(&log);
			}
			line = "";
		} else if (c == 0x1b) {
			// ESC [ parameters final-byte -- dropped entirely.
			int next = fgetc(pipe);
			if (next == '[') {
				while ((next = fgetc(pipe)) != EOF
						&& !(next >= 0x40 && next <= 0x7e))
					;
			}
		} else if (c >= 0x20 || c == '\t') {
			line << (char)c;
		}
	}
	if (line.Length() > 0) {
		result << line;
		BString trimmed(line);
		trimmed.Trim();
		if (IsWorthLoggingLine(trimmed)) {
			BMessage log(kMsgInstallLog);
			log.AddString("text", trimmed);
			target.SendMessage(&log);
		}
	}
	pclose(pipe);
	return result;
}

// After a successful install (never offered on uninstall), resolves the
// package's own Desktop-worthy target and asks -- synchronously, from
// this background job thread, the same way R InstallWizard asks its own
// post-install question from its own job thread rather than the window's
// -- whether to add it to the Desktop. Mirrors R InstallWizard's
// post-install step (see its AGENTS.md/PackageTools.cpp LaunchTargets),
// minus the system/home choice (R AppMarket only ever installs
// system-wide) and the multi-entry list (every R* package here is a
// single app, not a bundle).
static void
OfferDesktopIcon(const BString& name)
{
	BDirectory dir(kPackagesDir);
	if (dir.InitCheck() != B_OK)
		return;

	BString prefix(name);
	prefix << "-";
	BString hpkgName;
	BEntry entry;
	char entryName[B_FILE_NAME_LENGTH];
	dir.Rewind();
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (entry.GetName(entryName) != B_OK)
			continue;
		if (strncmp(entryName, prefix.String(), prefix.Length()) == 0) {
			hpkgName = entryName;
			break;
		}
	}
	if (hpkgName.Length() == 0)
		return;

	BString hpkgPath(kPackagesDir);
	hpkgPath << "/" << hpkgName;

	BString listCmd("package list -p '");
	listCmd << hpkgPath << "' 2>/dev/null";
	BString listing = RunCaptured(listCmd.String());

	BString displayName;
	BString targetPath;

	// A data/deskbar/menu/... entry is what the package itself chose to
	// show -- always installed as a real symlink, so what it resolves to
	// is read directly off disk rather than guessed from the listing.
	const char* p = listing.String();
	while (*p != '\0' && targetPath.Length() == 0) {
		const char* nl = strchr(p, '\n');
		size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
		BString line(p, (int32)len);
		line.Trim();
		if (line.FindFirst("data/deskbar/menu/") == 0) {
			BString installedPath("/boot/system/");
			installedPath << line;
			BEntry linkEntry(installedPath.String());
			if (linkEntry.IsSymLink()) {
				BEntry resolved(installedPath.String(), true);
				BPath resolvedPath;
				if (resolved.Exists() && !resolved.IsDirectory()
						&& resolved.GetPath(&resolvedPath) == B_OK) {
					int32 lastSlash = line.FindLast('/');
					displayName = lastSlash >= 0
						? BString(line.String() + lastSlash + 1) : line;
					targetPath = resolvedPath.Path();
				}
			}
		}
		if (nl == NULL)
			break;
		p = nl + 1;
	}

	// No Deskbar entry -- fall back to the package's own main apps/
	// binary, the same resolution icon-fetching already uses (see
	// FindMainBinaryPath).
	if (targetPath.Length() == 0) {
		BString binaryPath = FindMainBinaryPath(listing);
		if (binaryPath.Length() > 0) {
			targetPath = "/boot/system/";
			targetPath << binaryPath;
			int32 lastSlash = binaryPath.FindLast('/');
			displayName = lastSlash >= 0
				? BString(binaryPath.String() + lastSlash + 1) : binaryPath;
		}
	}

	if (targetPath.Length() == 0)
		return;

	BString question = TF(kAddDesktopIconQuestion, displayName.String());
	BAlert* alert = new BAlert(T(kWindowTitle), question.String(),
		T(kNoButton), T(kAddToDesktopButton));
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();
	if (choice != 1)
		return;

	BPath desktopPath;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktopPath) != B_OK)
		return;
	BDirectory desktop(desktopPath.Path());
	if (desktop.InitCheck() != B_OK || desktop.Contains(displayName.String()))
		return;
	desktop.CreateSymLink(displayName.String(), targetPath.String(), NULL);
}

// rust_bin's own REQUIRES includes gcc, binutils, gcc_syslibs_devel and
// haiku_devel -- a native build toolchain this minimal image doesn't ship
// and "RENKU Apps" doesn't carry either. Those four are vendored "Haiku
// Project", not "RENKU", so package_repo flatly refuses to index them
// alongside this repo's own R* packages (a mixed-vendor index is rejected
// outright) -- hence the separate "RENKU System" repo that already
// publishes exactly these four (see pkgman-repo/README.md). Its own
// `haiku` package lacks this image's media-stack fixes, though, so it's
// deliberately never one of R AppMarket's built-in repos: registering it
// permanently would risk a later, unrelated rchromium install silently
// pulling that lesser `haiku` in as a dependency. Added here just long
// enough to resolve rust_bin's own dependencies, then dropped again --
// confirmed safe (the already-installed `haiku` already satisfies
// haiku_devel's own requirement, so nothing about the base system changes)
// and confirmed to leave rust_bin/gcc/binutils/etc. installed and working
// after the repo is gone.
static const char* kSystemRepoURL = "https://pkgman.rainygirl.com/arm64-system";
static const char* kSystemRepoName = "RENKU System";

static int32
InstallThreadFunc(void* data)
{
	InstallJob* job = (InstallJob*)data;

	bool addedSystemRepo = false;
	if (job->install && job->name == "rust_bin" && !IsRepoKnown(kSystemRepoURL)) {
		BMessage repoLog(kMsgInstallLog);
		repoLog.AddString("text",
			"rust_bin needs a native toolchain -- adding the RENKU System repo for it...");
		job->target.SendMessage(&repoLog);

		BString addCmd("pkgman add-repo '");
		addCmd << kSystemRepoURL << "' < /dev/null >/dev/null 2>&1";
		RunCaptured(addCmd.String());
		addedSystemRepo = IsRepoKnown(kSystemRepoURL);
	}

	BString cmd("pkgman ");
	cmd << (job->install ? "install" : "uninstall") << " -y '";
	// Package names come only from packages.txt, a trusted source we just
	// fetched ourselves -- but quote defensively anyway.
	BString escaped(job->name);
	escaped.ReplaceAll("'", "'\\''");
	cmd << escaped << "' 2>&1";

	BString logLine("$ ");
	logLine << cmd;
	BMessage startLog(kMsgInstallLog);
	startLog.AddString("text", logLine);
	job->target.SendMessage(&startLog);

	BString output = RunCapturedStreaming(cmd.String(), job->target);

	if (addedSystemRepo) {
		BMessage dropLog(kMsgInstallLog);
		dropLog.AddString("text", "Removing the RENKU System repo again...");
		job->target.SendMessage(&dropLog);

		// drop-repo asks the same kind of interactive confirmation add-repo
		// does -- "echo yes |" answers it instead of leaving the prompt to
		// reread EOF forever (see IsRepoKnown's own note on add-repo).
		BString dropCmd("echo yes | pkgman drop-repo '");
		dropCmd << kSystemRepoName << "' >/dev/null 2>&1";
		RunCaptured(dropCmd.String());
	}

	bool success = IsPackageInstalled(job->name) == job->install;
	if (success && job->install)
		OfferDesktopIcon(job->name);

	BMessage done(kMsgOpDone);
	done.AddString("name", job->name);
	done.AddBool("install", job->install);
	done.AddBool("success", success);
	done.AddString("output", output);
	job->target.SendMessage(&done);

	delete job;
	return 0;
}

// Appends every non-blank, trimmed line of `listing` to `names` as a
// "package"/"repo"/"repourl" triple (parallel arrays, same index),
// skipping any package name already present -- a package can
// legitimately appear in more than one repo's packages.txt, and the
// first repo to list it wins the display tag.
static void
AppendUniqueLines(const BString& listing, const char* repo,
	const char* repoUrl, BMessage* names)
{
	const char* p = listing.String();
	while (*p != '\0') {
		const char* nl = strchr(p, '\n');
		size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);
		if (len > 0) {
			BString name(p, (int32)len);
			name.Trim();
			if (name.Length() > 0) {
				BString existing;
				bool found = false;
				for (int32 i = 0;
						names->FindString("package", i, &existing) == B_OK; i++) {
					if (existing == name) {
						found = true;
						break;
					}
				}
				if (!found) {
					names->AddString("package", name);
					names->AddString("repo", repo);
					names->AddString("repourl", repoUrl);
				}
			}
		}
		if (nl == NULL)
			break;
		p = nl + 1;
	}
}

static int32
RefreshThreadFunc(void* data)
{
	RefreshJob* job = (RefreshJob*)data;

	BMessage done(kMsgRefreshDone);
	bool any = false;
	for (int32 i = 0; i < job->repoUrls->CountItems(); i++) {
		const BString& url = *job->repoUrls->ItemAt(i);

		if (!IsRepoKnown(url.String())) {
			BString addCmd("pkgman add-repo '");
			addCmd << url << "' < /dev/null >/dev/null 2>&1";
			RunCaptured(addCmd.String());
		}

		BString listCmd("curl -fsS '");
		listCmd << url << "/packages.txt' 2>/dev/null";
		BString listing = RunCaptured(listCmd.String());
		if (listing.Length() > 0) {
			any = true;
			AppendUniqueLines(listing, HostFromURL(url.String()).String(),
				url.String(), &done);
		}
	}

	// A "shortdesc"/"fulldesc" pair per package, same index order as
	// "package" above (pkgman info is a local-cache read, ~30ms each --
	// cheap enough to do for the whole list here rather than lazily per
	// selection, so neither a row's blurb nor the detail panel's full
	// text ever makes the user wait).
	BString pkgName;
	for (int32 i = 0; done.FindString("package", i, &pkgName) == B_OK; i++) {
		BString shortDesc, fullDesc;
		FetchPackageInfo(pkgName, &shortDesc, &fullDesc);
		done.AddString("shortdesc", shortDesc);
		done.AddString("fulldesc", fullDesc);
	}

	done.AddBool("success", any);
	job->target.SendMessage(&done);

	delete job->repoUrls;
	delete job;
	return 0;
}

static const uint32 kMsgAddRepoConfirm = 'RARC';

// A small floating window for typing in a repo URL -- Haiku has no
// built-in single-line-input alert, so this is the minimal custom
// window that does the same job. Not modal: the main window stays
// usable while this is open, and closing this one (Cancel, the window
// close box, or Add) never affects it beyond the one kMsgAddRepo it
// sends on success.
class AddRepoWindow : public BWindow {
public:
	AddRepoWindow(BMessenger target)
		:
		BWindow(BRect(150, 150, 550, 230), T(kAddRepoWindowTitle),
			B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target)
	{
		fUrlControl = new BTextControl("url", T(kAddRepoURLLabel), "", NULL);

		BButton* addButton = new BButton("add", T(kAddRepoButton),
			new BMessage(kMsgAddRepoConfirm));
		// Cancel's own invocation message is the system "please close"
		// message directly -- BWindow already handles that by quitting,
		// so there's no need for a dedicated handler just to call Quit().
		BButton* cancelButton = new BButton("cancel", T(kCancelButton),
			new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
			.SetInsets(10)
			.Add(fUrlControl)
			.AddGroup(B_HORIZONTAL, 10)
				.AddGlue()
				.Add(cancelButton)
				.Add(addButton)
			.End();

		addButton->MakeDefault(true);
		fUrlControl->MakeFocus(true);
	}

	void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgAddRepoConfirm) {
			BString url(fUrlControl->Text());
			url.Trim();
			while (url.Length() > 0 && url[url.Length() - 1] == '/')
				url.Remove(url.Length() - 1, 1);
			if (url.Length() > 0) {
				BMessage add(kMsgAddRepo);
				add.AddString("url", url);
				fTarget.SendMessage(&add);
			}
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BMessenger fTarget;
	BTextControl* fUrlControl;
};

class AppMarketWindow : public BWindow {
public:
	AppMarketWindow()
		:
		BWindow(BRect(100, 100, 900, 760), T(kWindowTitle), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS),
		fAllPackages(20),
		fSortMode(kSortByPackage),
		fOperationInProgress(false),
		fActiveIconFetches(0),
		fIconGeneration(0)
	{
		fListView = new PackageListView("packages");
		fListView->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
		BScrollView* scroll = new BScrollView("scroll", fListView,
			0, false, true);

		fFilterMenu = new BPopUpMenu(T(kFilterAllRepos));
		fFilterMenu->AddItem(new BMenuItem(T(kFilterAllRepos),
			new BMessage(kMsgFilterChanged)));
		fFilterField = new BMenuField("repo-filter", T(kFilterLabel), fFilterMenu);

		fSortMenu = new BPopUpMenu(T(kSortByName));
		fSortMenu->AddItem(new BMenuItem(T(kSortByName), new BMessage(kMsgSortChanged)));
		fSortMenu->AddItem(new BMenuItem(T(kSortByRepo), new BMessage(kMsgSortChanged)));
		fSortMenu->ItemAt(0)->SetMarked(true);
		fSortField = new BMenuField("sort-by", T(kSortLabel), fSortMenu);
		fFilterMenu->SetTargetForItems(this);
		fSortMenu->SetTargetForItems(this);

		// SetModificationMessage, not the default invocation message --
		// that only fires on Enter/focus-loss, and a search box is
		// expected to filter live as each character is typed.
		fSearchField = new BTextControl("search", T(kSearchLabel), "",
			new BMessage(kMsgSearchChanged));
		fSearchField->SetModificationMessage(new BMessage(kMsgSearchChanged));
		fSearchField->SetExplicitMaxSize(BSize(220, B_SIZE_UNSET));

		fMenuBar = new BMenuBar("menubar");

		BMenu* fileMenu = new BMenu(T(kMenuFile));
		BMenuItem* quitItem = new BMenuItem(T(kMenuQuit),
			new BMessage(B_QUIT_REQUESTED), 'Q');
		fileMenu->AddItem(quitItem);
		quitItem->SetTarget(this);
		fMenuBar->AddItem(fileMenu);

		BMenu* repoMenu = new BMenu(T(kMenuRepos));
		BMenuItem* addRepoItem = new BMenuItem(T(kMenuAddRepo),
			new BMessage(kMsgShowAddRepo));
		repoMenu->AddItem(addRepoItem);
		addRepoItem->SetTarget(this);
		repoMenu->AddSeparatorItem();
		// Populated by _RebuildRemoveRepoMenu from fCustomRepos only -- a
		// built-in repo (kRepos) never appears here, so it can't be
		// removed (see IsBuiltinRepoURL).
		fRemoveRepoMenu = new BMenu(T(kMenuRemoveRepo));
		repoMenu->AddItem(fRemoveRepoMenu);
		fMenuBar->AddItem(repoMenu);

		LoadCustomRepos(&fCustomRepos);
		_RebuildRemoveRepoMenu();

		fDescriptionView = new BTextView("description");
		fDescriptionView->MakeEditable(false);
		fDescriptionView->SetWordWrap(true);
		fDescriptionView->SetInsets(4, 4, 4, 4);
		fDescriptionView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		BScrollView* descScroll = new BScrollView("description-scroll",
			fDescriptionView, 0, false, true, B_FANCY_BORDER);
		// Tall enough for about four or five lines before the vertical
		// scrollbar (already enabled -- see the BScrollView args above)
		// actually has to do anything; was two lines' worth before.
		descScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 160));
		descScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 220));

		fStatus = new BStringView("status", T(kLoading));

		// The name-only BView/BListView constructors leave the view with no
		// frame at all -- they're meant to be driven entirely by a BLayout,
		// not mixed with manual MoveTo/ResizeTo (which is what silently
		// produced a correctly-populated but zero-size, invisible list the
		// first time this was tried).
		// The menu bar sits outside the 10px inset group -- it's meant to
		// span the window edge to edge, like any other menu bar.
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fMenuBar)
			.AddGroup(B_VERTICAL, 10)
				.SetInsets(10)
				.AddGroup(B_HORIZONTAL, 10)
					.Add(fFilterField)
					.Add(fSortField)
					.AddGlue()
					.Add(fSearchField)
				.End()
				.Add(scroll, 3)
				.Add(descScroll, 1)
				.AddGroup(B_HORIZONTAL, 10)
					.Add(fStatus)
					.AddGlue()
				.End()
			.End();

		_Refresh();
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgRefreshDone:
				_HandleRefreshDone(message);
				break;

			case kMsgSelectionChanged:
			{
				// Both the description and the icon are already sitting on
				// the PackageItem itself -- fetched up front for the whole
				// list at refresh time (see _HandleRefreshDone and
				// _QueueIconFetches) -- so selecting something just shows
				// what's already there, no fetch, no wait.
				PackageItem* item
					= (PackageItem*)fListView->ItemAt(fListView->CurrentSelection());
				if (item != NULL) {
					fDescriptionView->SetText(item->FullDescription().Length() > 0
						? item->FullDescription().String() : T(kNoDescription));
				} else {
					fDescriptionView->SetText("");
				}
				break;
			}

			case kMsgAction:
			{
				// Sent by PackageListView::MouseDown with the specific row
				// clicked -- not necessarily (though usually, by then) the
				// current selection, so this reads straight from the index
				// it carries rather than fListView->CurrentSelection().
				int32 index = -1;
				message->FindInt32("index", &index);
				PackageItem* item = (PackageItem*)fListView->ItemAt(index);
				if (item != NULL && !fOperationInProgress)
					_StartInstallJob(!item->Installed(), item);
				break;
			}

			case kMsgOpDone:
				_HandleOpDone(message);
				break;

			case kMsgInstallLog:
			{
				BString text;
				message->FindString("text", &text);
				text << "\n";
				int32 end = fDescriptionView->TextLength();
				fDescriptionView->Insert(end, text.String(), text.Length());
				// BTextView scrolls to the selection on every insert, not
				// to whatever was just inserted -- the caret has to be
				// moved to the end itself, every time, or the view jumps
				// back to wherever it was left (see R InstallWizard's own
				// AGENTS.md note on the exact same behavior).
				int32 newEnd = fDescriptionView->TextLength();
				fDescriptionView->Select(newEnd, newEnd);
				fDescriptionView->ScrollToSelection();
				break;
			}

			case kMsgIconDone:
				_HandleIconDone(message);
				break;

			case kMsgFilterChanged:
				_RebuildVisibleList();
				break;

			case kMsgSortChanged:
				fSortMode = fSortMenu->FindMarkedIndex() == 1
					? kSortByRepoName : kSortByPackage;
				_RebuildVisibleList();
				break;

			case kMsgSearchChanged:
				_RebuildVisibleList();
				break;

			case kMsgShowAddRepo:
			{
				AddRepoWindow* addWindow = new AddRepoWindow(BMessenger(this));
				BRect mainFrame = Frame();
				addWindow->MoveTo(mainFrame.left + 60, mainFrame.top + 80);
				addWindow->Show();
				break;
			}

			case kMsgAddRepo:
				_HandleAddRepo(message);
				break;

			case kMsgRemoveRepo:
				_HandleRemoveRepo(message);
				break;

			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	void _Refresh()
	{
		fStatus->SetText(T(kFetchingList));

		PtrList<BString, true>* urls = new PtrList<BString, true>(8);
		const char* arch = CurrentArch();
		for (int32 i = 0; i < kRepoCount; i++) {
			if (strcmp(kRepos[i].arch, arch) == 0)
				urls->AddItem(new BString(kRepos[i].url));
		}
		for (int32 i = 0; i < fCustomRepos.CountItems(); i++)
			urls->AddItem(new BString(*fCustomRepos.ItemAt(i)));

		RefreshJob* job = new RefreshJob;
		job->target = BMessenger(this);
		job->repoUrls = urls;
		thread_id t = spawn_thread(RefreshThreadFunc, "appmarket-refresh",
			B_NORMAL_PRIORITY, job);
		if (t >= 0) {
			resume_thread(t);
		} else {
			delete urls;
			delete job;
		}
	}

	// Adding a repo that's already covered -- either a built-in one or
	// one already in fCustomRepos -- is a silent no-op rather than a
	// duplicate entry or an error dialog: harmless either way, and not
	// worth a confirmation the user didn't ask for.
	void _HandleAddRepo(BMessage* message)
	{
		BString url;
		if (message->FindString("url", &url) != B_OK || url.Length() == 0)
			return;

		if (IsBuiltinRepoURL(url))
			return;
		for (int32 i = 0; i < fCustomRepos.CountItems(); i++) {
			if (fCustomRepos.ItemAt(i)->ICompare(url) == 0)
				return;
		}

		fCustomRepos.AddItem(new BString(url));
		SaveCustomRepos(fCustomRepos);
		_RebuildRemoveRepoMenu();
		_Refresh();
	}

	void _HandleRemoveRepo(BMessage* message)
	{
		BString url;
		if (message->FindString("url", &url) != B_OK)
			return;

		for (int32 i = 0; i < fCustomRepos.CountItems(); i++) {
			if (*fCustomRepos.ItemAt(i) == url) {
				// RemoveItemAt detaches without deleting regardless of the
				// list's own Owning flag -- unlike RemoveItems(..., true)
				// used elsewhere in this file, it always leaves freeing
				// the item to the caller.
				delete fCustomRepos.RemoveItemAt(i);
				break;
			}
		}
		SaveCustomRepos(fCustomRepos);
		_RebuildRemoveRepoMenu();
		_Refresh();
	}

	// Lists only fCustomRepos -- a built-in repo (kRepos) never appears
	// here at all, so there's no way to remove one from this menu, by
	// design (see IsBuiltinRepoURL).
	void _RebuildRemoveRepoMenu()
	{
		fRemoveRepoMenu->RemoveItems(0, fRemoveRepoMenu->CountItems(), true);

		if (fCustomRepos.CountItems() == 0) {
			BMenuItem* placeholder = new BMenuItem(T(kMenuRemoveRepoEmpty), NULL);
			placeholder->SetEnabled(false);
			fRemoveRepoMenu->AddItem(placeholder);
			return;
		}

		for (int32 i = 0; i < fCustomRepos.CountItems(); i++) {
			const BString& url = *fCustomRepos.ItemAt(i);
			BMessage* msg = new BMessage(kMsgRemoveRepo);
			msg->AddString("url", url);
			BMenuItem* item = new BMenuItem(url.String(), msg);
			fRemoveRepoMenu->AddItem(item);
			item->SetTarget(this);
		}
	}

	void _HandleRefreshDone(BMessage* message)
	{
		bool success = false;
		message->FindBool("success", &success);
		if (!success) {
			fStatus->SetText(T(kCouldNotReach));
			return;
		}

		// fListView only ever holds pointers borrowed from fAllPackages (see
		// _RebuildVisibleList) -- clear it with RemoveItem(int32), which
		// does not delete, never MakeEmpty(), which does.
		for (int32 i = fListView->CountItems() - 1; i >= 0; i--)
			fListView->RemoveItem(i);
		// Old PackageItems are about to be deleted -- drop every view that
		// might still be pointing at one of them first.
		fDescriptionView->SetText("");
		fAllPackages.MakeEmpty();

		BString name, repo, repoUrl, shortDesc, fullDesc;
		for (int32 i = 0; message->FindString("package", i, &name) == B_OK; i++) {
			message->FindString("repo", i, &repo);
			message->FindString("repourl", i, &repoUrl);
			message->FindString("shortdesc", i, &shortDesc);
			message->FindString("fulldesc", i, &fullDesc);
			fAllPackages.AddItem(new PackageItem(name, repo, repoUrl,
				IsPackageInstalled(name), shortDesc, fullDesc));
		}

		_RebuildRepoFilterMenu();
		_RebuildVisibleList();
		_QueueIconFetches();
	}

	// Repopulates the filter dropdown from whichever repos actually showed
	// up in this refresh (not from kRepos -- a repo that matched the
	// architecture but came back empty shouldn't offer an empty filter).
	void _RebuildRepoFilterMenu()
	{
		BMenuItem* marked = fFilterMenu->FindMarked();
		BString previousSelection = marked != NULL ? marked->Label() : "";

		// RemoveItems(..., true) rather than RemoveItem()+delete in a loop:
		// BMenuField wraps fFilterMenu in its own internal menu bar, which
		// tracks the currently-displayed item to draw the field's closed-
		// state label. Deleting items by hand, one RemoveItem() at a time,
		// left that tracking holding a dangling pointer once the marked
		// item was gone, corrupting the heap in a way that only actually
		// crashed later and elsewhere (confirmed via a live crash: Haiku's
		// own debug_server caught a "bogus pointer (double free?)" inside
		// BList::_ResizeArray during a completely unrelated BListView
		// redraw). RemoveItems() is BMenu's own blessed bulk-removal path
		// and updates whatever BMenuField/the internal menu bar needs
		// updated as part of the same call, instead of leaving that for us
		// to get wrong by hand.
		fFilterMenu->RemoveItems(0, fFilterMenu->CountItems(), true);

		BMenuItem* allItem = new BMenuItem(T(kFilterAllRepos),
			new BMessage(kMsgFilterChanged));
		fFilterMenu->AddItem(allItem);
		allItem->SetTarget(this);

		PtrList<BString, true> seen(8);
		for (int32 i = 0; i < fAllPackages.CountItems(); i++) {
			const BString& repo = fAllPackages.ItemAt(i)->Repo();
			bool found = false;
			for (int32 j = 0; j < seen.CountItems(); j++) {
				if (*seen.ItemAt(j) == repo) {
					found = true;
					break;
				}
			}
			if (!found) {
				seen.AddItem(new BString(repo));
				BMenuItem* item = new BMenuItem(repo.String(),
					new BMessage(kMsgFilterChanged));
				fFilterMenu->AddItem(item);
				item->SetTarget(this);
				if (repo == previousSelection)
					item->SetMarked(true);
			}
		}

		if (fFilterMenu->FindMarked() == NULL)
			allItem->SetMarked(true);
	}

	// Rebuilds fListView's visible contents from fAllPackages according to
	// the current filter + search + sort selection. Never deletes a
	// PackageItem -- fAllPackages owns all of them regardless of which ones
	// are visible. Capped at kMaxVisiblePackages: a repo filter or search
	// can still leave hundreds of matches, and there's no reason to hand
	// BListView all of them at once when only the first couple of screens'
	// worth could ever be seen without scrolling past what the search was
	// narrowing down in the first place.
	void _RebuildVisibleList()
	{
		BMenuItem* marked = fFilterMenu->FindMarked();
		BString filterRepo = marked != NULL ? marked->Label() : "";
		bool showAll = marked == NULL || filterRepo == T(kFilterAllRepos);

		BString search(fSearchField->Text());
		search.Trim();
		bool hasSearch = search.Length() > 0;

		if (fSortMode == kSortByRepoName)
			fAllPackages.SortItems(_CompareByRepo);
		else
			fAllPackages.SortItems(_CompareByPackage);

		for (int32 i = fListView->CountItems() - 1; i >= 0; i--)
			fListView->RemoveItem(i);

		int32 matched = 0;
		int32 shown = 0;
		for (int32 i = 0; i < fAllPackages.CountItems(); i++) {
			PackageItem* item = fAllPackages.ItemAt(i);
			if (!showAll && item->Repo() != filterRepo)
				continue;
			if (hasSearch && item->PackageName().IFindFirst(search) < 0
					&& item->ShortDescription().IFindFirst(search) < 0)
				continue;

			matched++;
			if (shown < kMaxVisiblePackages) {
				fListView->AddItem(item);
				shown++;
			}
		}

		fStatus->SetText(shown < matched
			? TFInt2(kPackagesCountCapped, shown, matched).String()
			: TFInt(kPackagesCount, matched).String());
	}

	static int _CompareByPackage(const PackageItem* a, const PackageItem* b)
	{
		return a->PackageName().ICompare(b->PackageName());
	}

	static int _CompareByRepo(const PackageItem* a, const PackageItem* b)
	{
		int cmp = a->Repo().ICompare(b->Repo());
		return cmp != 0 ? cmp : a->PackageName().ICompare(b->PackageName());
	}

	void _StartInstallJob(bool install, PackageItem* item)
	{
		fOperationInProgress = true;
		PackageItem::sButtonsEnabled = false;
		item->SetBusy(true, install);
		fListView->Invalidate();

		fStatus->SetText(TF(install ? kInstalling : kRemoving,
			item->PackageName().String()).String());
		// Commandeered as this run's live log (see kMsgInstallLog) until
		// the user selects something else.
		fDescriptionView->SetText("");

		InstallJob* job = new InstallJob;
		job->target = BMessenger(this);
		job->name = item->PackageName();
		job->install = install;
		thread_id t = spawn_thread(InstallThreadFunc, "appmarket-install",
			B_NORMAL_PRIORITY, job);
		if (t >= 0) {
			resume_thread(t);
		} else {
			delete job;
			fOperationInProgress = false;
			PackageItem::sButtonsEnabled = true;
			item->SetBusy(false, install);
			fListView->Invalidate();
		}
	}

	void _HandleOpDone(BMessage* message)
	{
		BString name;
		bool install = false, success = false;
		message->FindString("name", &name);
		message->FindBool("install", &install);
		message->FindBool("success", &success);

		fOperationInProgress = false;
		PackageItem::sButtonsEnabled = true;

		// Looked up in fAllPackages, not fListView: the package being
		// installed might have been filtered out of the visible list by
		// the time its job finishes (the user switched the repo filter
		// mid-install), but its busy/installed state still has to be
		// cleared so the row is back to normal whenever it's shown again.
		for (int32 i = 0; i < fAllPackages.CountItems(); i++) {
			PackageItem* item = fAllPackages.ItemAt(i);
			if (item->PackageName() == name) {
				if (success)
					item->SetInstalled(install);
				item->SetBusy(false, install);
				break;
			}
		}
		fListView->Invalidate();

		StringKey key = success ? (install ? kInstalledDone : kRemovedDone) : kFailed;
		fStatus->SetText(TF(key, name.String()).String());
	}

	// Fills fIconQueue with every package just listed and starts fetching
	// (see _PumpIconQueue) -- called once per refresh, not per selection:
	// the whole list's icons load in the background regardless of what's
	// selected, capped at kMaxConcurrentIconFetches real .hpkg downloads
	// at a time. fIconGeneration is bumped so any job still in flight from
	// a previous refresh (its target PackageItems already deleted) is
	// recognized as stale and discarded instead of applied.
	void _QueueIconFetches()
	{
		fIconGeneration++;
		fIconQueue.MakeEmpty();
		for (int32 i = 0; i < fAllPackages.CountItems(); i++)
			fIconQueue.AddItem(fAllPackages.ItemAt(i));
		_PumpIconQueue();
	}

	void _PumpIconQueue()
	{
		while (fActiveIconFetches < kMaxConcurrentIconFetches
				&& fIconQueue.CountItems() > 0) {
			PackageItem* item = fIconQueue.RemoveItemAt(0);

			IconJob* job = new IconJob;
			job->target = BMessenger(this);
			job->name = item->PackageName();
			job->repoUrl = item->RepoUrl();
			job->generation = fIconGeneration;
			thread_id t = spawn_thread(IconThreadFunc, "appmarket-icon",
				B_LOW_PRIORITY, job);
			if (t >= 0) {
				fActiveIconFetches++;
				resume_thread(t);
			} else {
				delete job;
			}
		}
	}

	void _HandleIconDone(BMessage* message)
	{
		BString name, repoUrl;
		int32 generation = -1;
		message->FindString("name", &name);
		message->FindString("repourl", &repoUrl);
		message->FindInt32("generation", &generation);

		BBitmap* icon = NULL;
		message->FindPointer("icon", (void**)&icon);

		// A refresh happened while this fetch was still in flight -- the
		// PackageItem it was for is already gone, so just free the icon
		// and skip matching it to anything.
		if (generation != fIconGeneration) {
			delete icon;
		} else if (icon != NULL) {
			PackageItem* target = NULL;
			for (int32 i = 0; i < fAllPackages.CountItems(); i++) {
				PackageItem* candidate = fAllPackages.ItemAt(i);
				if (candidate->PackageName() == name
						&& candidate->RepoUrl() == repoUrl) {
					target = candidate;
					break;
				}
			}
			if (target != NULL) {
				target->SetIcon(icon);
				int32 visibleIndex = fListView->IndexOf(target);
				if (visibleIndex >= 0)
					fListView->InvalidateItem(visibleIndex);
			} else {
				delete icon;
			}
		}

		fActiveIconFetches--;
		_PumpIconQueue();
	}

	BListView* fListView;
	BPopUpMenu* fFilterMenu;
	BMenuField* fFilterField;
	BPopUpMenu* fSortMenu;
	BMenuField* fSortField;
	BTextControl* fSearchField;
	BMenuBar* fMenuBar;
	BMenu* fRemoveRepoMenu;
	BTextView* fDescriptionView;
	BStringView* fStatus;

	PtrList<PackageItem, true> fAllPackages;
	SortMode fSortMode;

	// True while some row's install/uninstall job is running -- see
	// _StartInstallJob/_HandleOpDone and PackageItem::sButtonsEnabled.
	bool fOperationInProgress;

	// Repos the user has added at runtime (see AddRepoWindow), persisted
	// via SaveCustomRepos/LoadCustomRepos -- kRepos itself never changes.
	PtrList<BString, true> fCustomRepos;

	// Background icon prefetch (see _QueueIconFetches/_PumpIconQueue).
	// fIconQueue holds borrowed pointers only -- fAllPackages above is
	// what owns every PackageItem.
	PtrList<PackageItem, false> fIconQueue;
	int32 fActiveIconFetches;
	int32 fIconGeneration;
};

class AppMarketApp : public BApplication {
public:
	AppMarketApp() : BApplication("application/x-vnd.RENKU-AppMarket") {}

	void ReadyToRun()
	{
		AppMarketWindow* window = new AppMarketWindow();
		window->Show();
	}
};

int
main()
{
	AppMarketApp app;
	app.Run();
	return 0;
}
