#include "autoprinter.h"
#include <QtGui/QApplication>
#include <windows.h>

int WatchDirectory(LPTSTR lpDir)
{
	DWORD dwWaitStatus; 
	HANDLE hChangeFolder; 

	// Watch the directory for file creation and deletion. 
	hChangeFolder = ::FindFirstChangeNotification( 
		lpDir,                         // directory to watch 
		FALSE,                         // do not watch subtree 
		FILE_NOTIFY_CHANGE_FILE_NAME); // watch file name changes 

	if (hChangeFolder == INVALID_HANDLE_VALUE || hChangeFolder == NULL) 
	{
		return -1;
	}


	// Change notification is set. Now wait on both notification 
	// handles and refresh accordingly. 
	while (TRUE) 
	{ 
		// Wait for notification.
		dwWaitStatus = ::WaitForSingleObject(hChangeFolder, INFINITE);

		switch (dwWaitStatus) 
		{ 
		case WAIT_OBJECT_0: 
			// A file was created, renamed, or deleted in the directory.
			// Refresh this directory and restart the notification.
			
			//TODO: Notify

			if (::FindNextChangeNotification(hChangeFolder) == FALSE)
			{
				return -1;
			}
			break; 

		case WAIT_TIMEOUT:
			// A timeout occurred, this would happen if some value other 
			// than INFINITE is used in the Wait call and no changes occur.
			// In a single-threaded environment you might not want an
			// INFINITE wait.
			if (::FindNextChangeNotification(hChangeFolder) == FALSE)
			{
				return -1;
			}
			break;

		default: 
			break;
		}
	}

	return 0;
}


int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	AutoPrinter w;
	w.show();
	return a.exec();
}
