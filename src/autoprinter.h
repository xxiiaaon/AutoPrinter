#ifndef AUTOPRINTER_H
#define AUTOPRINTER_H

#include <QtGui>
#include "ui_autoprinter.h"

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
class DirectoryMonitor : public QThread
{
	Q_OBJECT
public:
	DirectoryMonitor(const QString &strDir);
	~DirectoryMonitor();

	virtual void run();
	void Stop();

signals:
	void directoryChange();

private:
	QString m_strDir;
	bool m_bMonitor;
};

//=========================================================================
class AutoPrinter : public QMainWindow
{
	Q_OBJECT

public:
	AutoPrinter(QWidget *parent = 0, Qt::WFlags flags = 0);
	~AutoPrinter();

protected:
	virtual void paintEvent(QPaintEvent *event);

private:
	void CombineImage(const QString &strInputFile, const QString &strTemplateFile);
	void SaveSettings();

	// Tab initial functions.
	void InitTemplatePreview();

private slots:
	// Main functions.
	void OnMonitorFolderStart();
	void OnMonitorFolderStop();
	void OnSaveSettings();

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

private:
	Ui::AutoPrinterClass ui;

	QString m_strTemplateFile;
	QString m_strInputFile;
	QString m_strScanDir;
	QString m_strBackupDir;
	QString m_strOutputDir;

	QStringList m_listPndinProcFiles;
	
	QPoint m_FgImgPos; // Foreground Image Position
	QSize m_FgMaskSize; // Foreground Image Size

	CombineImageMaskReview* m_pMaskReviewWidget;
	DirectoryMonitor* m_pThreadMonitor;
};

#endif // AUTOPRINTER_H
