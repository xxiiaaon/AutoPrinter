#ifndef AUTOPRINTER_H
#define AUTOPRINTER_H

#include <QtGui/QMainWindow>
#include "ui_autoprinter.h"

class CombineImageMaskReview;
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

	// Template preview mask positon slider.
	void OnPosHorizontalChange();
	void OnPosVerticalChange();

private:
	Ui::AutoPrinterClass ui;

	QString m_strTemplateFile;
	QString m_strInputFile;
	QString m_strScanDir;
	QString m_strBackupDir;
	QString m_strOutputDir;
	
	QPoint m_FgImgPos; // Foreground Image Position
	QSize m_FgMaskSize; // Foreground Image Size

	CombineImageMaskReview* m_pMaskReviewWidget;
};

#endif // AUTOPRINTER_H
