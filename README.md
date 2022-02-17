# jenkins-builder

This is a simple project. I use Jenkins to build some static websites that I
serve behind Nginx. Since the static website data can be generated from scratch
at any time, I'm using volatile containers for storage of the build artifacts.
This application just ensures that a single build exists when the container
starts up.
