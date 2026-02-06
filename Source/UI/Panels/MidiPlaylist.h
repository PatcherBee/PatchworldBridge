/*
  ==============================================================================
    Source/Components/MidiPlaylist.h
    Playlist with folders / nested folders for library organization.
  ==============================================================================
*/
#pragma once
#include "../Fonts.h"
#include "../PopupMenuOptions.h"
#include "../Theme.h"
#include <deque>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

// --- Tree node for folder/file structure ---
struct PlaylistNode {
  bool isFolder = false;
  juce::String name;   // folder display name, or filename for files
  juce::String path;   // full path for files only
  std::vector<std::unique_ptr<PlaylistNode>> children;

  juce::String getDisplayName() const {
    if (isFolder)
      return name.isEmpty() ? "Folder" : name;
    return path.isEmpty() ? "?" : juce::File(path).getFileNameWithoutExtension();
  }
};

class PlaylistTreeItem;

// --- Playlist panel (full class before PlaylistTreeItem so owner->... resolves) ---
class MidiPlaylist : public juce::Component,
                     public juce::DragAndDropContainer,
                     public juce::DragAndDropTarget,
                     public juce::FileDragAndDropTarget {
public:
  juce::TreeView tree;
  std::unique_ptr<PlaylistNode> root;
  PlaylistTreeItem *rootItem = nullptr;
  std::unique_ptr<juce::FileChooser> chooser;
  juce::StringArray files;
  int currentIndex = 0;

  enum PlayMode { Single, LoopOne, LoopAll };
  PlayMode playMode = Single;
  juce::TextButton btnLoopMode{"Single"};
  std::function<void(juce::String)> onLoopModeChanged;
  std::function<void(const juce::String &)> onFileSelected;
  std::function<void(int)> onPlayModeChanged;
  std::function<void()> onLoadRequest;
  /** Called when user clicks Recent; pass the button so caller can show menu at it. */
  std::function<void(juce::Component *)> onRecentRequest;
  juce::TextButton btnClearPlaylist{"Clear"};
  juce::TextButton btnRecent{"Recent"};
  juce::TextButton btnNewFolder{"New folder"};
  juce::TextButton btnRandom{"RND"};
  bool shuffleEnabled = false;
  std::deque<int> shuffleHistory;
  juce::Label lblTitle{{}, ""};

  MidiPlaylist();
  ~MidiPlaylist() override;

  void rebuildFlatList();
  void collectPathsRecursive(PlaylistNode *n, juce::StringArray &out);
  int getFlatIndexForPath(const juce::String &path) const;
  void selectFileAtIndex(int index);
  PlaylistTreeItem *findItemForPath(PlaylistTreeItem *item,
                                    const juce::String &path);
  void refreshTree();
  void addFile(const juce::String &path);
  void addFileToFolder(PlaylistNode *folder, const juce::String &path);
  void addFilesToFolder(PlaylistNode *folder);
  void addFolderUnder(PlaylistNode *parent);
  void renameNode(PlaylistNode *node);
  void removeNode(PlaylistNode *node);
  PlaylistNode *findParent(PlaylistNode *parent, PlaylistNode *target);
  void rebuildFromRoot();
  void clear();
  juce::String getNextFile();
  juce::String getPrevFile();
  void savePlaylist();
  juce::var nodeToVar(PlaylistNode *n);
  void loadPlaylist();
  void varToNode(const juce::var &v, PlaylistNode *parent);
  void paint(juce::Graphics &g) override;
  bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails &) override;
  void itemDropped(const juce::DragAndDropTarget::SourceDetails &) override;
  bool isInterestedInFileDrag(const juce::StringArray &files_) override;
  void filesDropped(const juce::StringArray &files_, int x, int y) override;
  void resized() override;
};

// --- TreeView item that wraps a PlaylistNode ---
class PlaylistTreeItem : public juce::TreeViewItem {
public:
  PlaylistTreeItem(PlaylistNode *node, MidiPlaylist *owner)
      : node(node), owner(owner) {}

  juce::String getUniqueName() const override {
    if (!node)
      return "null";
    if (node->isFolder)
      return "f_" + node->name + "_" + juce::String(reinterpret_cast<uintptr_t>(node));
    return "file_" + node->path;
  }

  bool mightContainSubItems() override { return node && node->isFolder; }

  void itemOpennessChanged(bool isNowOpen) override {
    if (!isNowOpen) {
      clearSubItems();
      return;
    }
    if (!node || !node->isFolder)
      return;
    clearSubItems();
    for (auto &c : node->children)
      addSubItem(new PlaylistTreeItem(c.get(), owner));
    treeHasChanged();
  }

  /** Call after model (node->children) changes to refresh tree. */
  void refreshSubItems() {
    clearSubItems();
    if (node && node->isFolder) {
      for (auto &c : node->children) {
        auto *item = new PlaylistTreeItem(c.get(), owner);
        addSubItem(item);
        item->refreshSubItems();
      }
    }
    treeHasChanged();
  }

  void itemClicked(const juce::MouseEvent &e) override {
    if (!node || !owner)
      return;
    if (e.mods.isRightButtonDown()) {
      showContextMenu();
      return;
    }
    if (node->isFolder) {
      setOpen(!isOpen());
      return;
    }
    int idx = owner->getFlatIndexForPath(node->path);
    if (idx >= 0) {
      owner->currentIndex = idx;
      if (owner->onFileSelected)
        owner->onFileSelected(node->path);
    }
    TreeViewItem::itemClicked(e);
  }

  void itemDoubleClicked(const juce::MouseEvent &e) override {
    if (node && !node->isFolder && owner && owner->onFileSelected)
      owner->onFileSelected(node->path);
    TreeViewItem::itemDoubleClicked(e);
  }

  void paintItem(juce::Graphics &g, int width, int height) override {
    if (!node)
      return;
    bool itemSelected = isSelected();
    if (!node->isFolder && owner) {
      int flatIdx = owner->getFlatIndexForPath(node->path);
      if (flatIdx >= 0 && (flatIdx % 2 == 1))
        g.fillAll(Theme::bgPanel.withAlpha(0.4f));
    }
    if (itemSelected)
      g.fillAll(Theme::accent.withAlpha(0.3f));
    g.setColour(juce::Colours::white);
    g.setFont(Fonts::body().withHeight(14.0f));
    juce::String text = node->getDisplayName();
    if (node->isFolder && getNumSubItems() > 0)
      text += " (" + juce::String(getNumSubItems()) + ")";
    g.drawText(text, 4, 0, width - 8, height,
               juce::Justification::centredLeft, true);
  }

  void showContextMenu() {
    if (!node || !owner)
      return;
    juce::Component::SafePointer<MidiPlaylist> safeOwner(owner);
    PlaylistNode *nodePtr = node;
    juce::PopupMenu m;
    if (node->isFolder) {
      m.addItem("New subfolder", [safeOwner, nodePtr] {
        if (safeOwner != nullptr) safeOwner->addFolderUnder(nodePtr);
      });
      m.addItem("Add files here...", [safeOwner, nodePtr] {
        if (safeOwner != nullptr) safeOwner->addFilesToFolder(nodePtr);
      });
      m.addItem("Rename folder", [safeOwner, nodePtr] {
        if (safeOwner != nullptr) safeOwner->renameNode(nodePtr);
      });
      m.addSeparator();
    }
    m.addItem("Remove", [safeOwner, nodePtr] {
      if (safeOwner != nullptr) safeOwner->removeNode(nodePtr);
    });
    m.showMenuAsync(PopupMenuOptions::forComponent(owner));
  }

  PlaylistNode *node = nullptr;
  MidiPlaylist *owner = nullptr;
  std::vector<std::unique_ptr<PlaylistTreeItem>> cachedSubItems;
};

// --- MidiPlaylist method definitions (after PlaylistTreeItem for full type) ---
inline MidiPlaylist::MidiPlaylist() {
  root = std::make_unique<PlaylistNode>();
  root->isFolder = true;
  root->name = "";

  rootItem = new PlaylistTreeItem(root.get(), this);
  tree.setRootItem(rootItem);
  tree.setRootItemVisible(false);
  tree.setDefaultOpenness(true);
  tree.setIndentSize(14);
  tree.setColour(juce::TreeView::backgroundColourId,
                 juce::Colours::transparentBlack);
  addAndMakeVisible(tree);

  btnLoopMode.setColour(juce::TextButton::buttonColourId,
                        juce::Colours::grey.withAlpha(0.2f));
  btnLoopMode.setColour(juce::TextButton::textColourOffId,
                        juce::Colours::white);
  btnLoopMode.setButtonText("Loop Off");
  btnLoopMode.onClick = [this] {
    if (playMode == Single) {
      playMode = LoopOne;
      btnLoopMode.setButtonText("Loop One");
      btnLoopMode.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::cyan.darker(0.3f));
      if (onLoopModeChanged)
        onLoopModeChanged("Loop One");
    } else if (playMode == LoopOne) {
      playMode = LoopAll;
      btnLoopMode.setButtonText("Loop All");
      btnLoopMode.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::green.withAlpha(0.6f));
      if (onLoopModeChanged)
        onLoopModeChanged("Loop All");
    } else {
      playMode = Single;
      btnLoopMode.setButtonText("Loop Off");
      btnLoopMode.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::grey.withAlpha(0.2f));
      if (onLoopModeChanged)
        onLoopModeChanged("Loop Off");
    }
  };
  addAndMakeVisible(btnLoopMode);

  lblTitle.setFont(Fonts::bodyBold().withHeight(14.0f));
  lblTitle.setJustificationType(juce::Justification::centred);
  lblTitle.setColour(juce::Label::textColourId, Theme::accent);
  addAndMakeVisible(lblTitle);

  addAndMakeVisible(btnNewFolder);
  btnNewFolder.onClick = [this] { addFolderUnder(root.get()); };

  addAndMakeVisible(btnRecent);
  btnRecent.setTooltip("Open a recent .mid file (last 5).");
  btnRecent.onClick = [this] {
    if (onRecentRequest)
      onRecentRequest(&btnRecent);
  };

  addAndMakeVisible(btnClearPlaylist);
  btnClearPlaylist.onClick = [this] {
    juce::Component::SafePointer<MidiPlaylist> safe(this);
    juce::NativeMessageBox::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Clear playlist",
        "Remove all items from the playlist? This cannot be undone.",
        getTopLevelComponent(),
        juce::ModalCallbackFunction::create([safe](int result) {
          if (result == 1 && safe != nullptr) {
            safe->root->children.clear();
            safe->files.clear();
            safe->shuffleHistory.clear();
            safe->currentIndex = 0;
            safe->rebuildFlatList();
            safe->refreshTree();
          }
        }));
  };

  btnRandom.setClickingTogglesState(true);
  btnRandom.setColour(juce::TextButton::buttonOnColourId,
                      juce::Colours::orange.darker(0.2f));
  btnRandom.onClick = [this] {
    shuffleEnabled = btnRandom.getToggleState();
    shuffleHistory.clear();
  };
  addAndMakeVisible(btnRandom);

  setWantsKeyboardFocus(true);
}

inline MidiPlaylist::~MidiPlaylist() {
  tree.setRootItem(nullptr);
  rootItem = nullptr;
}

inline void MidiPlaylist::rebuildFlatList() {
    files.clear();
    collectPathsRecursive(root.get(), files);
  }

  inline void MidiPlaylist::collectPathsRecursive(PlaylistNode *n, juce::StringArray &out) {
    if (!n)
      return;
    if (n->isFolder) {
      for (auto &c : n->children)
        collectPathsRecursive(c.get(), out);
    } else if (n->path.isNotEmpty()) {
      out.add(n->path);
    }
  }

  inline int MidiPlaylist::getFlatIndexForPath(const juce::String &path) const {
    return files.indexOf(path);
  }

  /** Select the tree item for the file at flat index (for PlaybackController). */
  inline void MidiPlaylist::selectFileAtIndex(int index) {
    if (index < 0 || index >= files.size())
      return;
    juce::String path = files[index];
    PlaylistTreeItem *item = findItemForPath(rootItem, path);
    if (item) {
      item->setSelected(true, true);
      tree.scrollToKeepItemVisible(item);
    }
  }

  inline PlaylistTreeItem *MidiPlaylist::findItemForPath(PlaylistTreeItem *item,
                                    const juce::String &path) {
    if (!item || !item->node)
      return nullptr;
    if (!item->node->isFolder && item->node->path == path)
      return item;
    for (int i = 0; i < item->getNumSubItems(); ++i) {
      if (auto *sub = dynamic_cast<PlaylistTreeItem *>(item->getSubItem(i))) {
        if (auto *found = findItemForPath(sub, path))
          return found;
      }
    }
    return nullptr;
  }

  inline void MidiPlaylist::refreshTree() {
    if (rootItem)
      rootItem->refreshSubItems();
    tree.repaint();
  }

  inline void MidiPlaylist::addFile(const juce::String &path) {
    if (path.isEmpty())
      return;
    juce::File f(path);
    if (!f.existsAsFile() || !f.hasFileExtension("mid") && !f.hasFileExtension("midi"))
      return;
    if (files.contains(path))
      return;
    auto node = std::make_unique<PlaylistNode>();
    node->isFolder = false;
    node->name = f.getFileNameWithoutExtension();
    node->path = path;
    root->children.push_back(std::move(node));
    rebuildFlatList();
    refreshTree();
  }

  inline void MidiPlaylist::addFileToFolder(PlaylistNode *folder, const juce::String &path) {
    if (!folder || !folder->isFolder || path.isEmpty())
      return;
    if (files.contains(path))
      return;
    juce::File f(path);
    if (!f.existsAsFile())
      return;
    auto node = std::make_unique<PlaylistNode>();
    node->isFolder = false;
    node->name = f.getFileNameWithoutExtension();
    node->path = path;
    folder->children.push_back(std::move(node));
    rebuildFlatList();
    refreshTree();
  }

  inline void MidiPlaylist::addFilesToFolder(PlaylistNode *folder) {
    if (!folder || !folder->isFolder)
      folder = root.get();
    auto fileBrowserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser = std::make_unique<juce::FileChooser>("Add MIDI files", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(fileBrowserFlags, [this, folder](const juce::FileChooser &c) {
      for (const auto &f : c.getResults())
        if (f.existsAsFile())
          addFileToFolder(folder, f.getFullPathName());
    });
  }

  inline void MidiPlaylist::addFolderUnder(PlaylistNode *parent) {
    if (!parent || !parent->isFolder)
      parent = root.get();
    juce::String name = "New folder";
    int n = 1;
    for (;;) {
      bool taken = false;
      for (auto &c : parent->children)
        if (c->isFolder && c->name == name) {
          taken = true;
          break;
        }
      if (!taken)
        break;
      name = "New folder " + juce::String(++n);
    }
    auto node = std::make_unique<PlaylistNode>();
    node->isFolder = true;
    node->name = name;
    parent->children.push_back(std::move(node));
    refreshTree();
  }

  inline void MidiPlaylist::renameNode(PlaylistNode *node) {
    if (!node)
      return;
    juce::AlertWindow *w = new juce::AlertWindow("Rename", node->isFolder ? "Folder name:" : "Display name:",
                        juce::AlertWindow::NoIcon);
    w->addTextEditor("name", node->getDisplayName(), "Name", false);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    PlaylistNode *nodeToRename = node;
    w->enterModalState(true, juce::ModalCallbackFunction::create([w, nodeToRename, this](int result) {
      if (result == 1) {
        juce::String newName = w->getTextEditorContents("name").trim();
        if (newName.isNotEmpty()) {
          if (nodeToRename->isFolder)
            nodeToRename->name = newName;
          else
            nodeToRename->name = newName;
          refreshTree();
        }
      }
      delete w;
    }), true);
  }

  inline void MidiPlaylist::removeNode(PlaylistNode *node) {
    if (!node || node == root.get())
      return;
    PlaylistNode *parent = findParent(root.get(), node);
    if (!parent)
      return;
    int idx = -1;
    for (size_t i = 0; i < parent->children.size(); ++i) {
      if (parent->children[i].get() == node) {
        idx = (int)i;
        break;
      }
    }
    if (idx < 0)
      return;
    parent->children.erase(parent->children.begin() + idx);
    rebuildFlatList();
    if (currentIndex >= files.size() && files.size() > 0)
      currentIndex = (int)files.size() - 1;
    if (currentIndex < 0)
      currentIndex = 0;
    refreshTree();
    selectFileAtIndex(currentIndex);
  }

  inline PlaylistNode *MidiPlaylist::findParent(PlaylistNode *parent, PlaylistNode *target) {
    if (!parent || !target)
      return nullptr;
    for (auto &c : parent->children)
      if (c.get() == target)
        return parent;
    for (auto &c : parent->children) {
      if (auto *p = findParent(c.get(), target))
        return p;
    }
    return nullptr;
  }

  inline void MidiPlaylist::clear() {
    root->children.clear();
    files.clear();
    currentIndex = 0;
    rebuildFlatList();
    refreshTree();
  }

  inline juce::String MidiPlaylist::getNextFile() {
    if (files.isEmpty())
      return "";

    if (shuffleEnabled && files.size() > 1) {
      int maxHistory = juce::jmin((int)files.size() / 2, 4);
      juce::Random rng;
      int next = currentIndex;
      for (int attempt = 0; attempt < 20; ++attempt) {
        next = rng.nextInt(files.size());
        bool inHistory = false;
        for (auto idx : shuffleHistory) {
          if (idx == next) {
            inHistory = true;
            break;
          }
        }
        if (!inHistory && next != currentIndex)
          break;
      }
      shuffleHistory.push_back(next);
      while ((int)shuffleHistory.size() > maxHistory)
        shuffleHistory.pop_front();
      currentIndex = next;
    } else if (playMode == LoopOne) {
      // keep index same
    } else if (playMode == LoopAll) {
      currentIndex = (currentIndex + 1) % files.size();
    } else {
      if (currentIndex >= (int)files.size() - 1)
        return "";
      currentIndex++;
    }

    selectFileAtIndex(currentIndex);
    return files[currentIndex];
  }

  inline juce::String MidiPlaylist::getPrevFile() {
    if (files.isEmpty())
      return "";
    currentIndex = (currentIndex - 1 + files.size()) % files.size();
    selectFileAtIndex(currentIndex);
    return files[currentIndex];
  }

  inline void MidiPlaylist::savePlaylist() {
    juce::var treeVar = nodeToVar(root.get());
    auto dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("PatchworldBridge")
            .getChildFile("Profiles");
    if (!dir.exists())
      dir.createDirectory();
    juce::File f = dir.getChildFile("LastPlaylist.json");
    juce::DynamicObject *obj = new juce::DynamicObject();
    obj->setProperty("version", 2);
    obj->setProperty("tree", treeVar);
    f.replaceWithText(juce::JSON::toString(juce::var(obj)));
  }

  inline juce::var MidiPlaylist::nodeToVar(PlaylistNode *n) {
    if (!n)
      return {};
    auto *obj = new juce::DynamicObject();
    obj->setProperty("folder", n->isFolder);
    obj->setProperty("name", n->name);
    if (!n->isFolder && n->path.isNotEmpty())
      obj->setProperty("path", n->path);
    if (n->isFolder && !n->children.empty()) {
      juce::Array<juce::var> arr;
      for (auto &c : n->children)
        arr.add(nodeToVar(c.get()));
      obj->setProperty("children", arr);
    }
    return juce::var(obj);
  }

  inline void MidiPlaylist::loadPlaylist() {
    auto dir =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("PatchworldBridge")
            .getChildFile("Profiles");
    juce::File f = dir.getChildFile("LastPlaylist.json");
    if (!f.existsAsFile())
      return;

    juce::var data = juce::JSON::parse(f);
    if (data.isVoid() || data.isUndefined())
      return;

    root->children.clear();
    if (data.isArray()) {
      // Legacy: flat array of paths
      auto *arr = data.getArray();
      for (auto &v : *arr)
        addFile(v.toString());
      rebuildFlatList();
      refreshTree();
      return;
    }

    if (auto *obj = data.getDynamicObject()) {
      juce::var treeVar = obj->hasProperty("tree") ? obj->getProperty("tree")
                                                   : data;
      if (auto *treeObj = treeVar.getDynamicObject()) {
        if (treeObj->hasProperty("children")) {
          juce::var chVar = treeObj->getProperty("children");
          if (auto *arr = chVar.getArray())
            for (auto &c : *arr)
              varToNode(c, root.get());
        }
      }
    }
    rebuildFlatList();
    refreshTree();
  }

  inline void MidiPlaylist::varToNode(const juce::var &v, PlaylistNode *parent) {
    if (!parent || v.isVoid() || v.isUndefined())
      return;
    auto *obj = v.getDynamicObject();
    if (!obj)
      return;
    bool isFolder = (bool)obj->getProperty("folder");
    juce::String name = obj->getProperty("name").toString();
    juce::String path = obj->getProperty("path").toString();

    auto node = std::make_unique<PlaylistNode>();
    node->isFolder = isFolder;
    node->name = name;
    node->path = path;
    if (isFolder && obj->hasProperty("children")) {
      if (auto *arr = obj->getProperty("children").getArray())
        for (auto &child : *arr)
          varToNode(child, node.get());
    }
    parent->children.push_back(std::move(node));
  }

  inline void MidiPlaylist::paint(juce::Graphics &g) {
    auto r = getLocalBounds().toFloat();
    Theme::drawCardPanel(g, r, Theme::bgPanel.darker(0.08f), 6.0f);
    g.setColour(Theme::grid.withAlpha(0.15f));
    g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 1.0f);
    if (files.isEmpty() && (!root || root->children.empty())) {
      g.setColour(juce::Colours::grey);
      g.setFont(Fonts::body().withHeight(14.0f));
      g.drawText("Drag & Drop .mid or use New folder", getLocalBounds().withTrimmedTop(20),
                 juce::Justification::centred, true);
    }
  }

  inline bool MidiPlaylist::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails &) {
    return false;
  }

  inline void MidiPlaylist::itemDropped(const juce::DragAndDropTarget::SourceDetails &) {}

  inline bool MidiPlaylist::isInterestedInFileDrag(const juce::StringArray &files_) {
    for (const auto &p : files_)
      if (juce::File(p).hasFileExtension("mid") || juce::File(p).hasFileExtension("midi"))
        return true;
    return false;
  }

  inline void MidiPlaylist::filesDropped(const juce::StringArray &files_, int x, int y) {
    juce::ignoreUnused(x, y);
    for (const auto &p : files_) {
      juce::File f(p);
      if (f.existsAsFile() && (f.hasFileExtension("mid") || f.hasFileExtension("midi")))
        addFile(p);
    }
  }

  inline void MidiPlaylist::resized() {
    auto r = getLocalBounds();
    auto topRow = r.removeFromTop(25);
    btnLoopMode.setBounds(topRow.removeFromLeft(60));
    btnRandom.setBounds(topRow.removeFromLeft(40).reduced(2));
    btnNewFolder.setBounds(topRow.removeFromLeft(70).reduced(2));
    btnClearPlaylist.setBounds(topRow.removeFromRight(50).reduced(2));
    btnRecent.setBounds(topRow.removeFromRight(58).reduced(2));
    lblTitle.setBounds(topRow);
    tree.setBounds(r);
  }
