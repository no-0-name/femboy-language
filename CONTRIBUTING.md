# Contributing to Femboy

Thank you for your interest in contributing to Femboy! This document will help you get started.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [How to Contribute](#how-to-contribute)
- [Development Workflow](#development-workflow)
- [Coding Style](#coding-style)
- [Testing](#testing)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Feature Requests](#feature-requests)

---

## Code of Conduct

We are committed to providing a welcoming and inclusive environment for everyone. Please be respectful and kind to others. Harassment, discrimination, or offensive behavior will not be tolerated.

---

## Getting Started

### Prerequisites

- A C compiler (GCC, Clang, or MSVC)
- Make (optional, but recommended)
- Git

### Building the Project

```bash
# Clone the repository
git clone https://github.com/yourusername/femboy.git
cd femboy

# Build with Make
make

# Or manually
gcc -O2 -std=c11 -Wall -Wextra -Iinclude src/*.c -o femboy -lm

# Run a test program
./femboy examples/hello.fmb