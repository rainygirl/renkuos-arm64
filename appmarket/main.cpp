// R AppMarket -- a thin browser/installer GUI for a set of Haiku package
// repositories, filtered to whichever ones support the architecture this
// binary was actually built for (see kRepos/CurrentArch below). It does
// not reimplement dependency resolution or download/verification: it lists
// what each matching repo's packages.txt advertises and shells out to the
// already-trusted `pkgman` and `curl` for everything that touches the
// network or the package database, the same tools a user would run by
// hand in Terminal.
#include <Application.h>
#include <Button.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <StringView.h>
#include <Window.h>

#include <OS.h>

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
struct RepoDef {
	const char* name;
	const char* url;
	const char* arch; // "arm64", "x86_64", or "x86"
};

static const RepoDef kRepos[] = {
	{ "RENKU", "https://pkgman.rainygirl.com/arm64", "arm64" },
	{ "Spritz", "https://spritzdepot.desktoponfire.com/repo/x86_64/current", "x86_64" },
	{ "Grexe's", "https://kiri.sen-labs.org/x86_64", "x86_64" },
	{ "Besly", "https://software.besly.de/repo64", "x86_64" },
	{ "FattElk", "http://fatelk.com/repo_64", "x86_64" },
};
static const int32 kRepoCount = sizeof(kRepos) / sizeof(kRepos[0]);

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

class PackageItem : public BStringItem {
public:
	PackageItem(const BString& name, const BString& repo, bool installed)
		:
		BStringItem(""),
		fName(name),
		fRepo(repo),
		fInstalled(installed)
	{
		_UpdateText();
	}

	const BString& PackageName() const { return fName; }
	bool Installed() const { return fInstalled; }
	void SetInstalled(bool installed)
	{
		fInstalled = installed;
		_UpdateText();
	}

private:
	void _UpdateText()
	{
		// "[Repo] name" -- the repo tag comes first since several repos can
		// (in principle) list a package with the same name, and this is
		// the only place that distinction is visible to the user.
		BString text("[");
		text << fRepo << "] " << fName;
		if (fInstalled) {
			text << "  ";
			text << T(kInstalled);
		}
		SetText(text.String());
	}

	BString fName;
	BString fRepo;
	bool fInstalled;
};

struct InstallJob {
	BMessenger target;
	BString name;
	bool install; // true = install, false = uninstall
};

struct RefreshJob {
	BMessenger target;
};

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
			AppendUniqueLines(listing, repo.name, &done);
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
		BWindow(BRect(100, 100, 700, 760), T(kWindowTitle), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS)
	{
		fListView = new BListView("packages", B_SINGLE_SELECTION_LIST);
		fListView->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
		BScrollView* scroll = new BScrollView("scroll", fListView,
			0, false, true);

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
			.Add(scroll)
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
				if (item != NULL)
					fActionButton->SetLabel(item->Installed() ? T(kUninstall) : T(kInstall));
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

		fListView->MakeEmpty();
		BString name, repo;
		int32 count = 0;
		for (int32 i = 0; message->FindString("package", i, &name) == B_OK; i++) {
			message->FindString("repo", i, &repo);
			fListView->AddItem(
				new PackageItem(name, repo, IsPackageInstalled(name)));
			count++;
		}
		fStatus->SetText(TFInt(kPackagesCount, count).String());
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

	BListView* fListView;
	BStringView* fStatus;
	BButton* fRefreshButton;
	BButton* fActionButton;
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
