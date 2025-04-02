# GA Stippling

A genetic algorithm-based tool for converting images into stippled renditions, where the image is represented by a collection of dots. This technique is inspired by traditional art techniques and uses evolutionary algorithms to optimize dot placement.

## Overview

This application transforms regular images into stippled art by:

1. Processing an input image to black and white
2. Using a genetic algorithm to evolve a population of dot arrangements
3. Iteratively improving the dot positions to better match the original image

The result is a stylized version of the image composed entirely of dots (stipples), reminiscent of pointillist art.

## Features

- **Image Processing**: Convert images to black and white with adjustable blur and threshold
- **Genetic Algorithm**: Evolve dot patterns to match the target image
- **Real-time Visualization**: Watch the evolution process in real-time
- **Parameter Control**: Adjust the number of dots, blur amount, and threshold
- **Evolutionary Control**: Start/stop the evolution process at any time

## Installation

### Prerequisites

- Node.js (v14 or later)
- npm (v6 or later)

### Setup

1. Clone the repository:

   ```bash
   git clone https://github.com/Mfon-19/ga_stippling.git
   cd ga_stippling
   ```

2. Install dependencies:

   ```bash
   npm install
   ```

3. Start the development server:

   ```bash
   npm run dev
   ```

4. Open your browser and navigate to the URL shown in the terminal (typically http://localhost:5173)

## Usage

1. **Upload an Image**: Click the file input to select an image from your computer
2. **Adjust Parameters**:
   - **Blur**: Controls the blurring applied to the original image
   - **Threshold**: Determines the black/white cutoff point for the processed image
3. **Start Evolution**: Click "Start Evolution" to begin the genetic algorithm
4. **Monitor Progress**: Watch as the stippled representation evolves over time
5. **Stop Evolution**: Click "Stop Evolution" when satisfied with the result

## Optimization Tips

For best results:

- **Image Preparation**: Simple images with clear contrast work best
- **Blur Amount**:
  - Lower values (0-2): Preserve fine details but may create noisy stipples
  - Higher values (3-10): Create smoother stipple patterns but lose detail
- **Threshold**:
  - Lower values (<128): Include more dark areas from the original image
  - Higher values (>128): Create more white space in the result
- **Dot Count**:
  - Too few dots (<5000): Lose detail but create minimalist results
  - Too many dots (>100000): Better detail but slower evolution
- **Evolution Time**: Allow at least 100 generations for good results

## Technical Details

### Architecture

The application is built with TypeScript and follows a modular architecture:

```
src/
├── core/           # Genetic algorithm implementation
│   ├── Dot.ts      # Individual dot representation
│   ├── Individual.ts # Collection of dots (candidate solution)
│   ├── Population.ts # Collection of individuals (solution pool)
│   └── GeneticAlgorithm.ts # Evolution controller
├── ui/             # User interface components
│   ├── CanvasManager.ts # Canvas handling
│   └── EventHandlers.ts # User interaction
├── utils/          # Utility functions
│   ├── config.ts   # Configuration constants
│   └── ImageProcessor.ts # Image manipulation functions
├── types/          # TypeScript type definitions
│   └── index.ts    # Interface definitions
└── main.ts         # Application entry point
```

### Genetic Algorithm

The stippling algorithm uses a genetic approach:

1. **Initialization**: Random dot positions are generated
2. **Fitness Evaluation**: Each dot arrangement is compared to the target image
3. **Selection**: Better-performing arrangements are selected for reproduction
4. **Crossover**: New arrangements are created by combining features from parents
5. **Mutation**: Random changes are introduced to an Individual
6. **Replacement**: The population is updated with new candidate solutions
7. **Iteration**: The process repeats until stopped by the user

### Image Processing

The application uses canvas-based image processing to:

1. Convert the image to grayscale
2. Apply a box blur effect (user-configurable)
3. Apply a threshold to create a binary (black/white) image
4. Calculate statistics to determine optimal dot count

## Development

### Building for Production

To build the application for production:

```bash
npm run build
```

The compiled files will be in the `dist` directory.

### Project Structure

- **TypeScript**: For type safety and better code organization
- **Vite**: For fast development and optimization
- **Canvas API**: For image processing and rendering
- **Modular Design**: For better separation of concerns

## Future Development

Some potential enhancements for future versions:

- **Color Support**: Extend the algorithm to work with full-color images
- **Export Options**: Add SVG and high-resolution PNG export options
- **Performance Optimizations**: Implement Web Workers for faster evolution
- **Improved UI**: Enhanced user interface with better visualization options
