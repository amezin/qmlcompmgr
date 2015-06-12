import QtQuick 2.4
import QtQuick.Window 2.2
import QtGraphicalEffects 1.0
import Compositor 1.0

Item {
    id: root

    Component {
        id: windowComponent
        Item {
            id: windowRoot
            property var clientWindow

            x: clientWindow.geometry.x
            y: clientWindow.geometry.y
            width: windowPixmap.implicitWidth
            height: windowPixmap.implicitHeight
            opacity: 0
            scale: 0
            z: clientWindow.zIndex

            Component.onCompleted: {
                opacity = Qt.binding(function() { return clientWindow.mapped ? 1 : 0 })
                scale = Qt.binding(function() { return clientWindow.mapped ? 1 : 0 })
            }

            Connections {
                target: windowRoot.clientWindow
                onInvalidated: {
                    windowRoot.destroy(1000)
                }
            }

            Behavior on opacity {
                OpacityAnimator { }
            }

            Behavior on scale {
                ScaleAnimator { }
            }

            RectangularGlow {
                cached: true
                color: "black"
                opacity: 0.5
                glowRadius: 10
                anchors.fill: parent
                anchors.margins: -glowRadius / 2
            }

            WindowPixmap {
                id: windowPixmap
                clientWindow: windowRoot.clientWindow
            }
        }
    }

    Connections {
        target: compositor

        function createWindow(clientWindow) {
            windowComponent.createObject(root, { clientWindow: clientWindow })
        }

        onWindowCreated: {
            if (clientWindow.mapped) {
                createWindow(clientWindow)
            } else {
                var handler = (function (mapped) {
                    if (mapped) {
                        this.mapStateChanged.disconnect(handler)
                        createWindow(this)
                    }
                }).bind(clientWindow)
                clientWindow.mapStateChanged.connect(handler)
            }
        }
    }
}
