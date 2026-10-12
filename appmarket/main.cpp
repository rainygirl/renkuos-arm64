// R AppMarket -- a thin browser/installer GUI for a set of Haiku package
// repositories, filtered to whichever ones support the architecture this
// binary was actually built for (see kRepos/CurrentArch below). It does
// not reimplement dependency resolution or download/verification: it lists
// what each matching repo's packages.txt advertises and shells out to the
// already-trusted `pkgman` and `curl` for everything that touches the
// network or the package database, the same tools a user would run by
// hand in Terminal.
#include <AppFileInfo.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <OS.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Strings.h"
using namespace rappmarket;

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
static const uint32 kMsgRefresh = 'RRef';
static const uint32 kMsgSelectionChanged = 'RSel';
static const uint32 kMsgFilterChanged = 'RFlt';
static const uint32 kMsgSortChanged = 'RSrt';
static const uint32 kMsgInfoDone = 'RInf';

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

// Column layout, in pixels: a fixed icon column, then a fixed "Repo"
// column, then the package name filling the rest. Package names never
// overlap either of the first two, regardless of how the BListView
// itself is resized -- this is a fixed layout, not something that
// reflows.
static const float kIconColumnWidth = 32.0f;
static const float kRepoColumnWidth = 280.0f;
static const float kIconSize = 24.0f;

// A plain row: a generic app icon, repo name, then package name (plus an
// "(installed)" suffix), with a vertical rule between the text columns.
// BStringItem can't do separate cells at all -- it is exactly one string
// -- so this draws itself directly instead of just holding display text.
//
// Every row shares the same icon (sIcon, set once by AppMarketWindow from
// this app's own resource -- see RAppMarket.rdef): packages.txt is just
// names, nothing it advertises carries real per-package icon artwork, and
// downloading each listed package's .hpkg merely to read its icon before
// the user has asked to install anything would be a lot of network traffic
// for a cosmetic detail.
class PackageItem : public BListItem {
public:
	static BBitmap* sIcon;

	PackageItem(const BString& name, const BString& repo, bool installed)
		:
		BListItem(),
		fName(name),
		fRepo(repo),
		fInstalled(installed)
	{
	}

	const BString& PackageName() const { return fName; }
	const BString& Repo() const { return fRepo; }
	bool Installed() const { return fInstalled; }
	void SetInstalled(bool installed) { fInstalled = installed; }

	void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		SetHeight(std::max(ceilf(fh.ascent + fh.descent + fh.leading) + 6.0f,
			kIconSize + 4.0f));
	}

	void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetLowColor(background);
		owner->SetHighColor(background);
		owner->FillRect(frame);

		if (sIcon != NULL) {
			float y = frame.top + (frame.Height() - kIconSize) / 2.0f;
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(sIcon, BPoint(frame.left + 4, y));
			owner->SetDrawingMode(B_OP_COPY);
		}

		font_height fh;
		owner->GetFontHeight(&fh);
		float baseline = frame.top
			+ (frame.Height() - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

		rgb_color textColor = IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR);
		owner->SetHighColor(textColor);
		owner->SetLowColor(background);

		// Truncated with an ellipsis rather than just widening the column
		// to fit -- a repo tag is now a full hostname (see HostFromURL),
		// and an unusually long one should clip instead of pushing into
		// the package column.
		BString repoText(fRepo);
		owner->TruncateString(&repoText, B_TRUNCATE_END,
			kRepoColumnWidth - 8);
		owner->DrawString(repoText.String(),
			BPoint(frame.left + kIconColumnWidth, baseline));

		rgb_color ruleColor = tint_color(background, B_DARKEN_1_TINT);
		owner->SetHighColor(ruleColor);
		owner->StrokeLine(
			BPoint(frame.left + kIconColumnWidth + kRepoColumnWidth, frame.top),
			BPoint(frame.left + kIconColumnWidth + kRepoColumnWidth, frame.bottom));

		owner->SetHighColor(textColor);
		BString text(fName);
		if (fInstalled) {
			text << "  ";
			text << T(kInstalled);
		}
		owner->DrawString(text.String(),
			BPoint(frame.left + kIconColumnWidth + kRepoColumnWidth + 8, baseline));
	}

private:
	BString fName;
	BString fRepo;
	bool fInstalled;
};

BBitmap* PackageItem::sIcon = NULL;

// A plain two-label strip drawn above the list, aligned with PackageItem's
// own column layout -- BListView has no header of its own.
class ColumnHeaderView : public BView {
public:
	ColumnHeaderView()
		:
		BView("column-header", B_WILL_DRAW)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 20));
		SetExplicitMinSize(BSize(B_SIZE_UNSET, 20));
	}

	void Draw(BRect updateRect)
	{
		rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
		SetHighColor(tint_color(background, B_DARKEN_1_TINT));
		SetLowColor(background);
		FillRect(Bounds());

		font_height fh;
		GetFontHeight(&fh);
		float baseline = Bounds().top
			+ (Bounds().Height() - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(T(kColumnRepo), BPoint(kIconColumnWidth, baseline));
		DrawString(T(kColumnPackage),
			BPoint(kIconColumnWidth + kRepoColumnWidth + 8, baseline));

		StrokeLine(BPoint(Bounds().left, Bounds().bottom),
			BPoint(Bounds().right, Bounds().bottom));
	}
};

struct InstallJob {
	BMessenger target;
	BString name;
	bool install; // true = install, false = uninstall
};

struct RefreshJob {
	BMessenger target;
};

struct InfoJob {
	BMessenger target;
	BString name;
};

// `pkgman info` reads straight out of the local repo index this app
// already fetched via add-repo/refresh -- no network round-trip, no need
// to download the package itself just to read its description.
static int32
InfoThreadFunc(void* data)
{
	InfoJob* job = (InfoJob*)data;

	BString cmd("pkgman info '");
	BString escaped(job->name);
	escaped.ReplaceAll("'", "'\\''");
	cmd << escaped << "' 2>&1";

	BString output = RunCaptured(cmd.String());
	output.Trim();

	BMessage done(kMsgInfoDone);
	done.AddString("name", job->name);
	done.AddString("output", output);
	job->target.SendMessage(&done);

	delete job;
	return 0;
}

static int32
InstallThreadFunc(void* data)
{
	InstallJob* job = (InstallJob*)data;

	BString cmd("pkgman ");
	cmd << (job->install ? "install" : "uninstall") << " -y '";
	// Package names come only from packages.txt, a trusted source we just
	// fetched ourselves -- but quote defensively anyway.
	BString escaped(job->name);
	escaped.ReplaceAll("'", "'\\''");
	cmd << escaped << "' 2>&1";

	BString output = RunCaptured(cmd.String());

	BMessage done(kMsgOpDone);
	done.AddString("name", job->name);
	done.AddBool("install", job->install);
	done.AddBool("success", IsPackageInstalled(job->name) == job->install);
	done.AddString("output", output);
	job->target.SendMessage(&done);

	delete job;
	return 0;
}

// Appends every non-blank, trimmed line of `listing` to `names` as a
// "package"/"repo" pair (parallel arrays, same index), skipping any
// package name already present -- a package can legitimately appear in
// more than one repo's packages.txt, and the first repo to list it wins
// the display tag.
static void
AppendUniqueLines(const BString& listing, const char* repo, BMessage* names)
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
				}
			}
		}
		if (nl == NULL)
			break;
		p = nl + 1;
	}
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


static int32
RefreshThreadFunc(void* data)
{
	RefreshJob* job = (RefreshJob*)data;
	const char* arch = CurrentArch();

	BMessage done(kMsgRefreshDone);
	bool any = false;
	for (int32 i = 0; i < kRepoCount; i++) {
		const RepoDef& repo = kRepos[i];
		if (strcmp(repo.arch, arch) != 0)
			continue;

		if (!IsRepoKnown(repo.url)) {
			BString addCmd("pkgman add-repo '");
			addCmd << repo.url << "' < /dev/null >/dev/null 2>&1";
			RunCaptured(addCmd.String());
		}

		BString listCmd("curl -fsS '");
		listCmd << repo.url << "/packages.txt' 2>/dev/null";
		BString listing = RunCaptured(listCmd.String());
		if (listing.Length() > 0) {
			any = true;
			AppendUniqueLines(listing, HostFromURL(repo.url).String(), &done);
		}
	}

	done.AddBool("success", any);
	job->target.SendMessage(&done);

	delete job;
	return 0;
}

class AppMarketWindow : public BWindow {
public:
	AppMarketWindow()
		:
		BWindow(BRect(100, 100, 900, 760), T(kWindowTitle), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS),
		fAllPackages(20),
		fSortMode(kSortByPackage)
	{
		if (PackageItem::sIcon == NULL) {
			app_info info;
			if (be_app->GetAppInfo(&info) == B_OK) {
				BFile file(&info.ref, B_READ_ONLY);
				BAppFileInfo appFileInfo(&file);
				BBitmap* icon = new BBitmap(
					BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
				if (appFileInfo.GetIcon(icon, B_MINI_ICON) == B_OK)
					PackageItem::sIcon = icon;
				else
					delete icon;
			}
		}

		fListView = new BListView("packages", B_SINGLE_SELECTION_LIST);
		fListView->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
		BScrollView* scroll = new BScrollView("scroll", fListView,
			0, false, true);
		fHeader = new ColumnHeaderView();

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

		fDescriptionView = new BTextView("description");
		fDescriptionView->MakeEditable(false);
		fDescriptionView->SetWordWrap(true);
		fDescriptionView->SetInsets(4, 4, 4, 4);
		fDescriptionView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		BScrollView* descScroll = new BScrollView("description-scroll",
			fDescriptionView, 0, false, true, B_FANCY_BORDER);
		descScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 70));
		descScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 100));

		fStatus = new BStringView("status", T(kLoading));
		fRefreshButton = new BButton("refresh", T(kRefresh),
			new BMessage(kMsgRefresh));
		// One message, not kMsgInstall/kMsgUninstall picked at construction
		// time: the button's label is updated on every selection change
		// (see MessageReceived), and a BButton's invocation message doesn't
		// follow its label automatically. MessageReceived reads the
		// selected item's own Installed() state to decide which action a
		// click means, instead of relying on two things (label, message)
		// staying in sync.
		fActionButton = new BButton("action", T(kInstall),
			new BMessage(kMsgAction));
		fActionButton->SetEnabled(false);

		// The name-only BView/BListView constructors leave the view with no
		// frame at all -- they're meant to be driven entirely by a BLayout,
		// not mixed with manual MoveTo/ResizeTo (which is what silently
		// produced a correctly-populated but zero-size, invisible list the
		// first time this was tried).
		BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
			.SetInsets(10)
			.AddGroup(B_HORIZONTAL, 10)
				.Add(fFilterField)
				.Add(fSortField)
				.AddGlue()
			.End()
			.Add(fHeader)
			.Add(scroll, 3)
			.Add(descScroll, 1)
			.AddGroup(B_HORIZONTAL, 10)
				.Add(fStatus)
				.AddGlue()
				.Add(fRefreshButton)
				.Add(fActionButton)
			.End();

		_Refresh();
	}

	void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgRefresh:
				_Refresh();
				break;

			case kMsgRefreshDone:
				_HandleRefreshDone(message);
				break;

			case kMsgSelectionChanged:
			{
				PackageItem* item
					= (PackageItem*)fListView->ItemAt(fListView->CurrentSelection());
				fActionButton->SetEnabled(item != NULL);
				if (item != NULL) {
					fActionButton->SetLabel(item->Installed() ? T(kUninstall) : T(kInstall));
					_FetchDescription(item->PackageName());
				} else {
					fDescriptionView->SetText("");
				}
				break;
			}

			case kMsgAction:
			{
				PackageItem* item
					= (PackageItem*)fListView->ItemAt(fListView->CurrentSelection());
				if (item != NULL)
					_StartInstallJob(!item->Installed());
				break;
			}

			case kMsgOpDone:
				_HandleOpDone(message);
				break;

			case kMsgInfoDone:
				_HandleInfoDone(message);
				break;

			case kMsgFilterChanged:
				_RebuildVisibleList();
				break;

			case kMsgSortChanged:
				fSortMode = fSortMenu->FindMarkedIndex() == 1
					? kSortByRepoName : kSortByPackage;
				_RebuildVisibleList();
				break;

			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	void _Refresh()
	{
		fStatus->SetText(T(kFetchingList));
		fRefreshButton->SetEnabled(false);

		RefreshJob* job = new RefreshJob;
		job->target = BMessenger(this);
		thread_id t = spawn_thread(RefreshThreadFunc, "appmarket-refresh",
			B_NORMAL_PRIORITY, job);
		if (t >= 0)
			resume_thread(t);
		else
			delete job;
	}

	void _HandleRefreshDone(BMessage* message)
	{
		fRefreshButton->SetEnabled(true);
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
		fAllPackages.MakeEmpty();

		BString name, repo;
		for (int32 i = 0; message->FindString("package", i, &name) == B_OK; i++) {
			message->FindString("repo", i, &repo);
			fAllPackages.AddItem(
				new PackageItem(name, repo, IsPackageInstalled(name)));
		}

		_RebuildRepoFilterMenu();
		_RebuildVisibleList();
	}

	// Repopulates the filter dropdown from whichever repos actually showed
	// up in this refresh (not from kRepos -- a repo that matched the
	// architecture but came back empty shouldn't offer an empty filter).
	void _RebuildRepoFilterMenu()
	{
		BMenuItem* marked = fFilterMenu->FindMarked();
		BString previousSelection = marked != NULL ? marked->Label() : "";

		for (int32 i = fFilterMenu->CountItems() - 1; i >= 0; i--)
			delete fFilterMenu->RemoveItem(i);

		BMenuItem* allItem = new BMenuItem(T(kFilterAllRepos),
			new BMessage(kMsgFilterChanged));
		fFilterMenu->AddItem(allItem);
		allItem->SetTarget(this);

		BObjectList<BString, true> seen(8);
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
	// the current filter + sort selection. Never deletes a PackageItem --
	// fAllPackages owns all of them regardless of which ones are visible.
	void _RebuildVisibleList()
	{
		BMenuItem* marked = fFilterMenu->FindMarked();
		BString filterRepo = marked != NULL ? marked->Label() : "";
		bool showAll = marked == NULL || filterRepo == T(kFilterAllRepos);

		if (fSortMode == kSortByRepoName)
			fAllPackages.SortItems(_CompareByRepo);
		else
			fAllPackages.SortItems(_CompareByPackage);

		for (int32 i = fListView->CountItems() - 1; i >= 0; i--)
			fListView->RemoveItem(i);

		int32 shown = 0;
		for (int32 i = 0; i < fAllPackages.CountItems(); i++) {
			PackageItem* item = fAllPackages.ItemAt(i);
			if (showAll || item->Repo() == filterRepo) {
				fListView->AddItem(item);
				shown++;
			}
		}
		fStatus->SetText(TFInt(kPackagesCount, shown).String());
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

	void _StartInstallJob(bool install)
	{
		PackageItem* item
			= (PackageItem*)fListView->ItemAt(fListView->CurrentSelection());
		if (item == NULL)
			return;

		fActionButton->SetEnabled(false);
		fRefreshButton->SetEnabled(false);
		fStatus->SetText(TF(install ? kInstalling : kRemoving,
			item->PackageName().String()).String());

		InstallJob* job = new InstallJob;
		job->target = BMessenger(this);
		job->name = item->PackageName();
		job->install = install;
		thread_id t = spawn_thread(InstallThreadFunc, "appmarket-install",
			B_NORMAL_PRIORITY, job);
		if (t >= 0)
			resume_thread(t);
		else
			delete job;
	}

	void _HandleOpDone(BMessage* message)
	{
		fRefreshButton->SetEnabled(true);
		BString name;
		bool install = false, success = false;
		message->FindString("name", &name);
		message->FindBool("install", &install);
		message->FindBool("success", &success);

		for (int32 i = 0; i < fListView->CountItems(); i++) {
			PackageItem* item = (PackageItem*)fListView->ItemAt(i);
			if (item->PackageName() == name) {
				if (success)
					item->SetInstalled(install);
				fListView->InvalidateItem(i);
				if (fListView->CurrentSelection() == i) {
					fActionButton->SetEnabled(true);
					fActionButton->SetLabel(item->Installed() ? T(kUninstall) : T(kInstall));
				}
				break;
			}
		}

		StringKey key = success ? (install ? kInstalledDone : kRemovedDone) : kFailed;
		fStatus->SetText(TF(key, name.String()).String());
	}

	// `pkgman info` reads the local repo index, so this is fast, but it's
	// still a subprocess -- kept off the UI thread like every other
	// pkgman/curl call here. fPendingInfoRequest guards against a stale
	// reply landing after the user has already selected something else.
	void _FetchDescription(const BString& name)
	{
		fPendingInfoRequest = name;
		fDescriptionView->SetText(T(kLoading));

		InfoJob* job = new InfoJob;
		job->target = BMessenger(this);
		job->name = name;
		thread_id t = spawn_thread(InfoThreadFunc, "appmarket-info",
			B_NORMAL_PRIORITY, job);
		if (t >= 0)
			resume_thread(t);
		else
			delete job;
	}

	void _HandleInfoDone(BMessage* message)
	{
		BString name, output;
		message->FindString("name", &name);
		message->FindString("output", &output);

		// The user may have selected a different package while this was
		// in flight -- only apply it if it's still the one showing.
		if (name != fPendingInfoRequest)
			return;

		if (output.Length() == 0)
			output = T(kNoDescription);
		fDescriptionView->SetText(output.String());
	}

	BListView* fListView;
	ColumnHeaderView* fHeader;
	BPopUpMenu* fFilterMenu;
	BMenuField* fFilterField;
	BPopUpMenu* fSortMenu;
	BMenuField* fSortField;
	BTextView* fDescriptionView;
	BStringView* fStatus;
	BButton* fRefreshButton;
	BButton* fActionButton;
	BString fPendingInfoRequest;

	BObjectList<PackageItem, true> fAllPackages;
	SortMode fSortMode;
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
