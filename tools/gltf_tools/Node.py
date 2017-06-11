class Node():
    def __init__(self):
        self.mesh = None
        self.translation = (0.0, 0.0, 0.0)
        self.scale = (1.0, 1.0, 1.0)
        self.rotation = (0.0, 0.0, 0.0, 1.0)
        self.children = []
