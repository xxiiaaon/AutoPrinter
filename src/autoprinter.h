#ifndef AUTOPRINTER_H
#define AUTOPRINTER_H

#include "ui_autoprinter.h"

#include <windows.h>
#include <QtGui>
#include <QSettings>

//=========================================================================
class DirectoryMonitorThread : public QThread
{
	Q_OBJECT
public:
	DirectoryMonitorThread(const QString &strDir);
	~DirectoryMonitorThread();

	virtual void run();
	void Stop();

signals:
	void directoryChange();

private:
	bool m_bRun;
	QString m_strMonDir;
	HANDLE m_hMonDir;
};

//=========================================================================
class PhotoCombinerInterface
{
public:
	virtual void CombineImage(const QString &strInputImage) = 0;
};

class PhotoPrinterInterface
{
public:
	virtual void PrintImage(const QString &strImagePath) = 0;
};

//=========================================================================
class PhotoCombineThread : public QThread
{
	Q_OBJECT
public:
	PhotoCombineThread(PhotoCombinerInterface* pPhotoCombiner);
	virtual void run();

private:
	bool m_bRun;
	PhotoCombinerInterface* m_pPhotoCombiner;
};

//=========================================================================
class PrinterThread : public QThread
{
	Q_OBJECT
public:
	PrinterThread(PhotoPrinterInterface* pPhotoPrinter);
	virtual void run();

private:
	bool m_bRun;
	PhotoPrinterInterface* m_pPhotoPrinter;
};

//=========================================================================
class AutoScaledDisplayWidget : public QWidget
{
	Q_OBJECT
public:
	AutoScaledDisplayWidget(QWidget *parent = 0);
	void SetDisplayImage(const QImage &image);

	virtual void paintEvent(QPaintEvent *event);

private:
	QImage m_image;
};

//=========================================================================
class AutoPrinter 
	: public QMainWindow
	, public PhotoCombinerInterface
	, public PhotoPrinterInterface
{
	Q_OBJECT
public:
	AutoPrinter(QWidget *parent = 0, Qt::WFlags flags = 0);
	~AutoPrinter();

	virtual void CombineImage(const QString &strInputImage);
	virtual void PrintImage(const QString &strImagePath);

protected:
	bool eventFilter(QObject *obj, QEvent *event);

private:
	void SaveSettings();
	QImage GetFramePreviewImage();
	void UpdateMaskConfigRange();
	void InitialPrinter();

	// Tab initial functions.
	void InitPreviewTab();
	void InitPrinterTab();
	void InitOutputTab();

	// Image process, image print thread synchronization functions.
	void AddPendingPrintImage(const QString &strImagePath, int nTimes = 1, bool bPreempt = false);

private slots:
	// Main functions.
	void OnMonitorFolderStart();
	void OnMonitorFolderStop();
	void OnSaveSettings();
	void OnPrintCopy();

	// Settings.
	void OnSelectScanDir();
	void OnSelectBackupDir();
	void OnSelectOutputDir();
	void OnSelectTemplatePath();

	// Mask width(height), Mask Coordinate.
	void OnMaskWidthChange(int val);
	void OnMaskHeightChange(int val);
	void OnMaskHeightChangeSB(int val);
	void OnMaskWidthChangeSB(int val);
	void OnMaskCoordChangeX(int val);
	void OnMaskCoordChangeY(int val);
	void OnMaskCoordChangeXSB(int val); // For spinbox.
	void OnMaskCoordChangeYSB(int val); // For spinbox.

	// Thread relatives.
	void OnMonitorDirChange();

	// Tab initial.
	void OnCurrentChanged(int nIndex);

	// Output tab's relatives.
	void OnOuputItemSelected(const QModelIndex &index);
	void OnFindOutputFileName(const QString& string);
	void OnUpdateOutputList();

	// Printer tab's relatives.
	void OnUpdatePrintingList();
	void OnPrintingItemSelected(const QModelIndex &index);
	void OnCancelPrintItem();

private:
	Ui::AutoPrinterClass ui;

	QString m_strTmptFilePath;
	QString m_strInputFile;
	QString m_strScanDir;
	QString m_strBackupDir;
	QString m_strOutputDir;

	QPoint	m_FgMaskPos; // Foreground Image Position
	QSize	m_FgMaskSize; // Foreground Image Size

	AutoScaledDisplayWidget* m_pMaskReviewWidget;
	AutoScaledDisplayWidget* m_pOutputDispWidget;
	AutoScaledDisplayWidget* m_pPrintReviewWidget;

	DirectoryMonitorThread* m_pThreadMonitor;
	PhotoCombineThread*		m_pThreadCombine;
	PrinterThread*			m_pThreadPrint;

	QSettings*	m_pSettings;
	QImage		m_imgTemplate;
	QPrinter*	m_pPrinter;
};

#endif // AUTOPRINTER_H
