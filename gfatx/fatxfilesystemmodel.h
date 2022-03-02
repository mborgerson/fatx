#ifndef FATXFILESYSTEMMODEL_H
#define FATXFILESYSTEMMODEL_H

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <fatx.h>
#include <assert.h>

#include <string>
#include <memory>

//Disk
//    Partition (FS)
//        Files/Directories

enum NodeType {
    NODETYPE_DISK,
    NODETYPE_PARTITION,
    NODETYPE_FILE,
};

struct NodeBase {
    enum NodeType type;
    struct NodeBase *parent;
    std::vector<std::unique_ptr<struct NodeBase>> children;
    int rowInParent;
};

struct NodeDisk : NodeBase {

};

struct NodePartition : NodeBase {
    struct fatx_fs *fs;
    std::string name;
};

struct NodeFile : NodeBase
{
    struct fatx_attr attr;
    std::string path;
};

class FatxFileSystemModel : public QAbstractItemModel
{
protected:
    struct NodeDisk m_root;

    void populateFileNodeChildren(struct fatx_fs *fs, struct NodeBase *node)
    {
        struct fatx_dir dir;
        struct fatx_dirent dirent, *nextdirent;
        struct fatx_attr attr;
        int s;

        const char *path;
        if (node->type == NODETYPE_PARTITION) {
            path = "/";
        } else if (node->type == NODETYPE_FILE){
            auto fsnode = static_cast<struct NodeFile *>(node);
            if (!(fsnode->attr.attributes & FATX_ATTR_DIRECTORY)) {
                return;
            }
            path = fsnode->path.c_str();
        }

        s = fatx_open_dir(fs, path, &dir);
        assert(s == FATX_STATUS_SUCCESS);

        for (int i = 0; true; i++) {
            s = fatx_read_dir(fs, &dir, &dirent, &attr, &nextdirent);
            if (s != FATX_STATUS_SUCCESS) break;

            std::string child_path("");
            if (node->type != NODETYPE_PARTITION) {
                child_path += static_cast<struct NodeFile *>(node)->path;
            }
            child_path += "/";
            child_path += attr.filename;

            auto child = new struct NodeFile;
            child->type = NODETYPE_FILE;
            child->parent = node;
            child->attr = attr;
            child->path = child_path;
            child->rowInParent = i;
            node->children.emplace_back(child);

            s = fatx_next_dir_entry(fs, &dir);
            if (s != FATX_STATUS_SUCCESS) break;
        }

        fatx_close_dir(fs, &dir);

        // XXX: It would be better to populate this on demand when a user
        // expands a directory.
        for (auto &child : node->children) {
            populateFileNodeChildren(fs, static_cast<struct NodeFile *>(child.get()));
        }
    }

public:
    void addPartition(std::string name, struct fatx_fs *fs)
    {
        auto node = new NodePartition;
        node->type = NODETYPE_PARTITION;
        node->parent = &m_root;
        node->name = name;
        node->fs = fs;
        node->rowInParent = m_root.children.size();
        populateFileNodeChildren(fs, node);
        m_root.children.emplace_back(node);
    }

    FatxFileSystemModel()
    {
        m_root.parent = nullptr;
        m_root.type = NODETYPE_DISK;
    }

    QModelIndex index(int row, int column, const QModelIndex &parent) const
    {
        const struct NodeBase *parent_node;

        if (parent.isValid()) {
            parent_node = static_cast<struct NodeBase *>(parent.internalPointer());
        } else {
            parent_node = &m_root;
        }

        assert(row < parent_node->children.size());
        return createIndex(row, column, parent_node->children[row].get());
    }

    bool hasChildren(const QModelIndex &parent) const
    {
        return rowCount(parent) > 0;
    }

    QModelIndex parent(const QModelIndex &child) const
    {
        if (child.isValid()) {
            auto node = static_cast<struct NodeBase *>(child.internalPointer());
            if (node->parent) {
                return createIndex(node->rowInParent, 0, node->parent);
            }
        }

        return QModelIndex();
    }

    int rowCount(const QModelIndex &parent) const
    {
        const struct NodeBase *parent_node;

        if (parent.isValid()) {
            parent_node = static_cast<struct NodeBase *>(parent.internalPointer());
        } else {
            parent_node = &m_root;
        }

        return parent_node->children.size();
    }

    int columnCount(const QModelIndex &parent) const
    {
        return 3;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const
    {
        if (role == Qt::DisplayRole) {
            QString headers[3] = {
                "Name", "Size", "Type"
            };
            return headers[section];
        }

        return QVariant();
    }

    QVariant data(const QModelIndex &index, int role) const
    {
        if (!index.isValid()) {
            return QVariant();
        }

        if (role == Qt::DisplayRole) {
            auto node = static_cast<struct NodeBase *>(index.internalPointer());

            if (index.column() == 0) {
                if (node->type == NODETYPE_PARTITION) {
                    return QString(static_cast<struct NodePartition *>(node)->name.c_str());
                } else if (node->type == NODETYPE_FILE) {
                    return QString::fromLocal8Bit(static_cast<struct NodeFile *>(node)->attr.filename);
                }
            } else if (index.column() == 1) {
                if (node->type == NODETYPE_FILE) {
                    auto fsnode = static_cast<struct NodeFile *>(node);
                    if (fsnode->attr.attributes & FATX_ATTR_DIRECTORY) {
                        return QVariant();
                    } else {
                        return QString("%1").arg(fsnode->attr.file_size);
                    }
                }
            } else if (index.column() == 2) {
                if (node->type == NODETYPE_PARTITION) {
                    return QString("Partition");
                } else if (node->type == NODETYPE_FILE) {
                    auto fsnode = static_cast<struct NodeFile *>(node);
                    if (fsnode->attr.attributes & FATX_ATTR_DIRECTORY) {
                        return QString("Directory");
                    } else {
                        return QString("File");
                    }
                }
            }
        }

        if (role == Qt::DecorationRole) {
            auto node = static_cast<struct NodeFile *>(index.internalPointer());
            if (index.column() == 0) {
                QFileIconProvider iconProvider;
                if (node->type == NODETYPE_PARTITION) {
                    return iconProvider.icon(QFileIconProvider::IconType::Drive);
                } else if (node->type == NODETYPE_FILE) {
                    if (node->attr.attributes & FATX_ATTR_DIRECTORY) {
                        return iconProvider.icon(QFileIconProvider::IconType::Folder);
                    } else {
                        return iconProvider.icon(QFileIconProvider::IconType::File);
                    }
                }
            }
        }

        return QVariant();
    }
};

#endif // FATXFILESYSTEMMODEL_H
