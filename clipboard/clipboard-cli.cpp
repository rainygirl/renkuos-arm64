// clipboard-cli get|set -- a thin CLI over Haiku's BClipboard, so a remote
// (ssh) caller without a GUI session can read or write the Haiku clipboard.
#include <Application.h>
#include <Clipboard.h>
#include <Message.h>
#include <String.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv)
{
	if (argc < 2 || (strcmp(argv[1], "get") != 0 && strcmp(argv[1], "set") != 0)) {
		fprintf(stderr, "usage: %s get|set\n", argc > 0 ? argv[0] : "clipboard-cli");
		return 1;
	}

	BApplication app("application/x-vnd.RENKU-clipboard-cli");

	if (strcmp(argv[1], "get") == 0) {
		if (!be_clipboard->Lock())
			return 1;
		BMessage* clip = be_clipboard->Data();
		const char* text = NULL;
		ssize_t len = 0;
		if (clip != NULL
			&& clip->FindData("text/plain", B_MIME_TYPE, (const void**)&text, &len) == B_OK) {
			fwrite(text, 1, (size_t)len, stdout);
		}
		be_clipboard->Unlock();
		return 0;
	}

	// set: read stdin in full, then replace the clipboard with it.
	BString text;
	char buf[4096];
	ssize_t n;
	while ((n = read(0, buf, sizeof(buf))) > 0)
		text.Append(buf, (int32)n);

	if (!be_clipboard->Lock())
		return 1;
	be_clipboard->Clear();
	BMessage* clip = be_clipboard->Data();
	status_t status = B_ERROR;
	if (clip != NULL)
		status = clip->AddData("text/plain", B_MIME_TYPE, text.String(), text.Length());
	if (status == B_OK)
		status = be_clipboard->Commit();
	be_clipboard->Unlock();
	return status == B_OK ? 0 : 1;
}
