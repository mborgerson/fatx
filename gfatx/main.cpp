#include "mainwindow.h"
#include <cstdio>

#include <QApplication>
#include <QFileSystemModel>
#include <QFileIconProvider>
#include <QTreeView>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QScreen>
#include <QGuiApplication>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>

#include "fatxfilesystemmodel.h"

/* Retail layout fallback. E's length is 0x1312D6000 (the old 0x131F00000
 * value overshot the F boundary). F and G only exist via a partition table
 * or the fixed F offset; their entries are resolved at runtime below.
 */
struct PartitionSlot {
    const char *letter;
    uint64_t offset;
    uint64_t size;
    fatx_fs fs;
} partition_map[] = {
    { "C", 0x8ca80000, 0x01f400000, {} },
    { "E", 0xabe80000, 0x1312d6000, {} },
    { "X", 0x00080000, 0x02ee00000, {} },
    { "Y", 0x2ee80000, 0x02ee00000, {} },
    { "Z", 0x5dc80000, 0x02ee00000, {} },
    { "F", 0x1dd156000, 0, {} },
    { "G", 0, 0, {} },
};

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    QCommandLineParser parser;
    parser.setApplicationDescription("gfatx");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dontUseCustomDirectoryIconsOption("c", "Set QFileIconProvider::DontUseCustomDirectoryIcons");
    parser.addOption(dontUseCustomDirectoryIconsOption);
    parser.addPositionalArgument("disk", "The disk image to open.");
    parser.process(app);

    if (parser.positionalArguments().isEmpty()) {
        fprintf(stderr, "Specify path to disk\n");
        exit(1);
    }

    const QString diskPath = parser.positionalArguments().first();
    QByteArray ba = diskPath.toLocal8Bit();
    const char *disk_path = ba.data();

    FatxFileSystemModel model;
    for (int i = 0; i < ARRAY_SIZE(partition_map); i++) {
        int s;
        uint64_t offset = partition_map[i].offset;
        uint64_t size   = partition_map[i].size;

        /* An XBpartitioner table on the disk overrides the fixed layout and
         * is the only source of custom F/G locations (#37/#75).
         */
        if (fatx_disk_read_partition_map(disk_path,
                                         tolower(partition_map[i].letter[0]),
                                         &offset, &size) != FATX_STATUS_SUCCESS) {
            if (size == 0) {
                /* F without a table: extends to the end of the disk. */
                uint64_t disk_size;
                if (offset == 0 || fatx_disk_size(disk_path, &disk_size) ||
                    offset >= disk_size) {
                    continue; /* G has no fixed fallback location */
                }
                size = disk_size - offset;
            }
        }

        fatx_log_init(&partition_map[i].fs, stderr, 1);
        s = fatx_open_device(&partition_map[i].fs, disk_path,
                             offset, size,
                             512, FATX_READ_FROM_SUPERBLOCK);
        if (s != FATX_STATUS_SUCCESS) {
            fprintf(stderr, "No filesystem in partition %s\n",
                    partition_map[i].letter);
            continue;
        }
        model.addPartition(std::string(partition_map[i].letter), &partition_map[i].fs);
    }

    QTreeView tree;
    tree.setModel(&model);
    tree.setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(&tree, &QTreeView::customContextMenuRequested,
                     [&](const QPoint &pos) {
        QModelIndex index = tree.indexAt(pos);
        if (!index.isValid()) return;
        QMenu menu;
        QAction *extract = menu.addAction(QObject::tr("Extract to..."));
        if (menu.exec(tree.viewport()->mapToGlobal(pos)) != extract) return;
        QString dir = QFileDialog::getExistingDirectory(&tree,
            QObject::tr("Extract to directory"));
        if (dir.isEmpty()) return;
        int n = model.extractIndex(index.siblingAtColumn(0), dir);
        if (n < 0) {
            QMessageBox::warning(&tree, "gfatx", QObject::tr("Extraction failed."));
        } else {
            QMessageBox::information(&tree, "gfatx",
                QObject::tr("Extracted %1 file(s).").arg(n));
        }
    });
    tree.setAnimated(false);
    tree.setIndentation(20);
    tree.setSortingEnabled(true);
    QScreen *screen = QGuiApplication::screenAt(tree.pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QSize availableSize = screen->availableGeometry().size();
        tree.resize(availableSize / 2);
    }

    tree.setColumnWidth(0, tree.width() / 3);

    tree.setWindowTitle(QObject::tr("gfatx"));
    tree.show();

    return app.exec();
}
