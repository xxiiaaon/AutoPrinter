#ifndef AUTOPRINTER_H
#define AUTOPRINTER_H

#include "ui_autoprinter.h"

#include <windows.h>
#include <QtGui>
#include <QSettings>

//=========================================================================
class CombineImageMaskReview : public QWidget
{
	Q_OBJECT
public:
	CombineImageMaskReview(const QString &strImage);

	QSize GetTemplateImageSize() const;
	void SetMaskPosX(int posX);
	void SetMaskPosY(int posY);
	void SetMaskSize(const QSize &size);

protected:
	virtual void paintEvent(QPaintEvent *event);

private:
	QImage m_imgTemplate;
	int m_nPosX;
	int m_nPosY;
	QSize m_sizeMask;
};
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
class PhotoCombiner
{
public:
	virtual void CombineImage(const QString &strInputImage) = 0;
};

class PhotoPrinter
{
public:
	virtual void PrintImage(const QString &strImagePath) = 0;
};

//=========================================================================
class PhotoCombineThread : public QThread
{
	Q_OBJECT
public:
	PhotoCombineThread(PhotoCombiner* pPhotoCombiner);
	virtual void run();

private:
	bool m_bRun;
	PhotoCombiner* m_pPhotoCombiner;
};

//=========================================================================
class PrinterThread : public QThread
{
	Q_OBJECT
public:
	PrinterThread(PhotoPrinter* pPhotoPrinter);
	virtual void run();

private:
	bool m_bRun;
	PhotoPrinter* m_pPhotoPrinter;
};

//=========================================================================
class OuputDisplayWidget : public QWidget
{
	Q_OBJECT
public:
	OuputDisplayWidget(QWidget *parent = 0);
	void SetDisplayImage(const QImage &image);

	virtual void paintEvent(QPaintEvent *event);

private:
	QImage m_image;
};

//=========================================================================
class AutoPrinter 
	: public QMainWindow
	, public PhotoCombiner
	, public PhotoPrinter
{
	Q_OBJECT
public:
	AutoPrinter(QWidget *parent = 0, Qt::WFlags flags = 0);
	~AutoPrinter();

	virtual void CombineImage(const QString &strInputImage);
	virtual void PrintImage(const QString &strImagePath);

protected:
	virtual void paintEvent(QPaintEvent *event);
	virtual void resizeEvent(QResizeEvent *event);

private:
	void SaveSettings();

	// Tab initial functions.
	void InitTemplatePreviewTab();
	void InitPrinterSelectTab();
	void InitCombineOutputTab();

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

	// Template preview mask position slider.
	void OnPosHorizontalChange();
	void OnPosVerticalChange();

	// Mask width(height), Mask Coordinate.
	void OnMaskHeightChange(int val);
	void OnMaskWidthChange(int val);
	void OnMaskCoordChangeX();
	void OnMaskCoordChangeY();

	// Thread relatives.
	void OnMonitorDirChange();

	// Tab initial.
	void OnCurrentChanged(int nIndex);

	// Output tab's relatives.
	void OnOuputItemSelected(const QModelIndex &index);
	void OnFindOutputFileName(const QString& string);
	void OnUpdateOutputList();

private:
	Ui::AutoPrinterClass ui;

	QString m_strTmptFilePath;
	QString m_strInputFile;
	QString m_strScanDir;
	QString m_strBackupDir;
	QString m_strOutputDir;

	QPoint m_FgImgPos; // Foreground Image Position
	QSize m_FgMaskSize; // Foreground Image Size

	CombineImageMaskReview* m_pMaskReviewWidget;
	DirectoryMonitorThread* m_pThreadMonitor;
	PhotoCombineThread*		m_pThreadCombine;
	PrinterThread*			m_pThreadPrint;

	QSettings* m_pSettings;
};

#endif // AUTOPRINTER_H
