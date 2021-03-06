
#include "deform_volume.h"

// Warper Class
Deform_Volume::Deform_Volume()
{
  ;
}

Deform_Volume::Deform_Volume(CGAL::Image_3 *image, map<string, FT> options)
{

  min_stretch = options["min_stretch_distance"];
  max_stretch = options["max_stretch_distance"];

  stretch_probability = options["stretch_probability"];
  dilate_probability = options["dilate_probability"];
  disable_xyz_stretch_probability = options["disable_xyz_stretch_probability"]; // Higher value means more likely to disable

  image_data = (unsigned char *)image->data();
  xdim = image->xdim();
  ydim = image->ydim();
  zdim = image->zdim();

  vx = image->vx();
  vy = image->vy();
  vz = image->vz();

  find_mesh_bounds();

  deformation_info = "";
}

/* Image data is represented (to humans) as 3D voxel data e.g. array[x][y][z]. To CGAL,
  it is just a one dimensional array which contains all of the voxels.

  Return the one dimensional array location corresponding to a
  particular voxel in the image.
  */
long Deform_Volume::get_array_index(int x, int y, int z)
{

  long idx;
  idx = (x * xdim + y) * ydim + z;

  if (idx >= xdim * ydim * zdim)
  {
    //std::cout << "Array index too big!" << std::endl;
    return -1;
  }

  if (x < 0 || y < 0 || z < 0)
  {
    //std:cout << "Negative value passed!" << std::endl;
    return -1;
  }

  return idx;
}

/* Expands one a particular layer/tissue by one voxel on all sides
  layer_index: the layer/tissue index to be dilated
  n_pixels: how many pixels/voxels to dilate the layer by
  */
void Deform_Volume::dilate_layer(int layer_index, int n_pixels)
{

  cout << "Dilating layer " + to_string(layer_index) + " by " + to_string(n_pixels) + " pixels" << endl;

  unsigned char layer_index_char = (unsigned char)layer_index;

  long n_elements = xdim * ydim * zdim;
  long vector_element, i;
  vector<long> neighbours;
  unordered_set<long> elements_to_change;

  // Append dilation info to deformation description
  deformation_info += 
    "Dilating layer: " + to_string(layer_index) + " by " + to_string(n_pixels) + " pixels.\n";

  // Loop for each level of pixel dilation
  while (n_pixels--)
  {
    for (i = 0; i < n_elements; i++)
    {
      if (image_data[i] == layer_index_char)
      {
        // Get the neighbouring elements
        neighbours = neighbouring_elements(i);

        // Add all neighbours to the list of elements to change
        while (!neighbours.empty())
        {

          vector_element = neighbours.back();
          neighbours.pop_back();
          elements_to_change.insert(vector_element);
        }
      }
    }

    // Do the dilation
    // Go through the elements and change the layer type
    unordered_set<long>::iterator itr;
    for (itr = elements_to_change.begin(); itr != elements_to_change.end(); itr++)
    {
      image_data[*itr] = layer_index_char;
    }
  }
}

/* Calculate the indexes of elements neighbouring a particular voxel in 3D array
  */
vector<long> Deform_Volume::neighbouring_elements(long voxel_index)
{

  int x, y, z; //Indexes of voxel

  x = voxel_index / (xdim * ydim);
  y = voxel_index % (ydim * xdim) / ydim;
  z = voxel_index - ((x * xdim) + y) * ydim;

  vector<long> neighbours;

  int i, j, k;
  long this_idx;

  for (i = x - 1; i <= x + 1; i++)
  {
    for (j = y - 1; j <= y + 1; j++)
    {
      for (k = z - 1; k <= z + 1; k++)
      {

        this_idx = get_array_index(i, j, k);

        // Check that voxel is within bounds [0 NDIMS] and that it is not the original voxel
        if (i >= 0 && i < xdim && j >= 0 && j < ydim && k >= 0 && k < zdim &&
            this_idx != voxel_index)
        {
          neighbours.push_back(this_idx);
        }
      }
    }
  }

  return neighbours;
}

/* Stretch the image
  vector<int> stretch: 9 element vector (3 for x, 3 for y, 3 for z) that gives the parameters
  of the stretch to be performed. For each dimension:
    1st element - point at which stretch starts
    2nd element - distance this point will be moved
    3rd element - anchor point. The stretching distance for each elment is relative
    to its distance from this point.

    Only points between the move point and the anchor point are stretched.

    */
void Deform_Volume::stretch_array(vector<int> stretch)
{

  // Print stretch parameters
  cout << "Stretching with parameters:" << endl;
  int vec_i;

  for (vec_i = 0; vec_i < stretch.size(); ++vec_i)
  {
    cout << stretch[vec_i] << " ";
  }
  cout << endl;

  int this_idx, idx_to_copy_from;
  int new_x, new_y, new_z;

  double min_stretch_ratio;

  // Create stretch objects in each dimension
  Mesh_Stretcher x(stretch[0], stretch[1], stretch[2], xdim);
  Mesh_Stretcher y(stretch[3], stretch[4], stretch[5], ydim);
  Mesh_Stretcher z(stretch[6], stretch[7], stretch[8], zdim);

  // Loop over each element that is affected by the stretch
  int x_idx, y_idx, z_idx;
  for (x_idx = x.start_iterate; x_idx != x.end_iterate; x_idx += x.step)
  {
    for (y_idx = y.start_iterate; y_idx != y.end_iterate; y_idx += y.step)
    {
      for (z_idx = z.start_iterate; z_idx != z.end_iterate; z_idx += z.step)
      {

        x.prepare_stretch(x_idx);
        y.prepare_stretch(y_idx);
        z.prepare_stretch(z_idx);

        //In order to avoid irregular warping of the mesh,
        // need to use the same stretch ratio for all dimensions,
        // which should be the minimum value.
        // Inactive dimensions (Which aren't being stretched) have a ratio of 1, so are ignored.
        min_stretch_ratio = min(abs(x.stretch_ratio), abs(y.stretch_ratio));
        min_stretch_ratio = min(min_stretch_ratio, abs(z.stretch_ratio));

        x.stretch_ratio = min_stretch_ratio;
        y.stretch_ratio = min_stretch_ratio;
        z.stretch_ratio = min_stretch_ratio;

        // Calculate where to copy from
        new_x = x.idx_to_copy_from(x_idx);
        new_y = y.idx_to_copy_from(y_idx);
        new_z = z.idx_to_copy_from(z_idx);

        // Copy point over
        this_idx = get_array_index(z_idx, y_idx, x_idx);
        idx_to_copy_from = get_array_index(new_z, new_y, new_x);
        image_data[this_idx] = image_data[idx_to_copy_from];
      }
    }
  }

  deformation_info += "Stretch: \n";
  deformation_info += "x: " + x.stretch_description();
  deformation_info += "y: " + y.stretch_description();
  deformation_info += "z: " + z.stretch_description();
}

/* Randomly stretch/dilate the data */
void Deform_Volume::modify_image()
{
  std::cout << endl
            << "MODIFYING IMAGE DATA" << endl;

  string mesh_stretcher = "";

  srand(time(NULL));

  // Do at least one deformation
  random_stretch();

  // Do more streches?
  while ((rand() / double(RAND_MAX)) < stretch_probability)
  {
    random_stretch();
  }

  // Do some dilation?
  while ((rand() / double(RAND_MAX)) < dilate_probability)
  {
    random_dilate();
  }

  find_mesh_bounds(); //Update edges of object
}

void Deform_Volume::random_stretch()
{
  vector<int> stretch;

  //TODO: point_to_move, distance and anchor are class variables, is this needed anymore?
  //x data
  int max_distance;

  point_to_move = random_stretch_point(xmin, xmax, xdim);
  anchor = random_anchor_point(xmin, xmax);
  max_distance = min(max_stretch, min(point_to_move, xdim - point_to_move));
  distance_to_move = min_stretch + rand() % max_distance;

  stretch.push_back(point_to_move);
  stretch.push_back(distance_to_move);
  stretch.push_back(anchor);

  //y data
  point_to_move = random_stretch_point(ymin, ymax, ydim);
  anchor = random_anchor_point(ymin, ymax);
  max_distance = min(max_stretch, min(point_to_move, ydim - point_to_move));
  distance_to_move = min_stretch + rand() % max_distance;

  stretch.push_back(point_to_move);
  stretch.push_back(distance_to_move);
  stretch.push_back(anchor);

  //z data
  point_to_move = random_stretch_point(zmin, zmax, zdim);
  anchor = random_anchor_point(zmin, zmax);
  max_distance = min(max_stretch, min(point_to_move, ydim - point_to_move));
  distance_to_move = min_stretch + rand() % max_distance;

  stretch.push_back(point_to_move);
  stretch.push_back(distance_to_move);
  stretch.push_back(anchor);

  //Randomly turn on or off stretching in each dimension.
  // Stretch is off is set to -1
  int divisor = round(1.0 / disable_xyz_stretch_probability);
  
  if (!(rand() % divisor))  {
    stretch[0] = -1; //x
  }

  if (!(rand() % divisor))  {
    stretch[3] = -1; //y
  }

  if (!(rand() % divisor))  {
    stretch[6] = -1; //z
  }

  stretch_array(stretch);
}

/** Return a stretch point that is < idx_min or > idx_max
    Pick  a point 1 voxels away from the maximum/minimum
    Check it is not outside of the array (> dim or < 0)
    **/
int Deform_Volume::random_stretch_point(int idx_min, int idx_max, int dim)
{
  int upper = idx_max + 1;
  int lower = idx_min - 1;

  // Make sure they are within bounds of array
  upper = min(upper, dim);
  lower = max(lower, 0);

  if (rand() % 2)
  {
    return upper;
  }

  return lower;
}

int Deform_Volume::random_anchor_point(int idx_min, int idx_max)
{
  // Want to pick an anchor point  'within' the object
  // i.e. >idx_min and < idx_max
  return idx_min + rand() % (idx_max - idx_min);
}

/** Dilate a random layer by a random amount (either  1 or 2 pixels)
 **/

void Deform_Volume::random_dilate()
{
  int layer, dilate_amount;
  int max_dilate = 1;

  // Get list of layer indices and select one at random
  get_layers();
  int random_index = rand() % layers.size();
  layer = layers[random_index];

  dilate_amount = 1 + rand() % max_dilate;

  dilate_layer(layer, dilate_amount);
}

/** Loop through data array and populate a vector of all the unique values
  **/
void Deform_Volume::get_layers()
{
  layers.clear(); // Empty the layers vector, so that elements aren't added twice.

  // Set maximum number of layers to some unreasonably high value, so that
  // we always have space.
  int max_layers = 100;
  int layer_seen[max_layers] = {0};

  // Loop over all elements in mesh. Use hash map to keep track of layers
  // that we have seen.
  int current_layer;
  long total_elements = xdim * ydim * zdim;

  for (int i = 0; i < total_elements; i++)
  {
    current_layer = image_data[i];
    layer_seen[current_layer] = 1;
  }

  // Add all layers that we have seen to the 'layers' vector.
  // Start at 1 as 0 is the 'empty' layer
  for (int i = 1; i < max_layers; i++)
  {
    if (layer_seen[i] == 1)
      layers.push_back(i);
  }
}

/** Find the first/last non 0 element along each dimensional
  * by looping over all elements
  **/
void Deform_Volume::find_mesh_bounds()
{
  // reset bounds
  xmax = INT_MIN, ymax = INT_MIN, zmax = INT_MIN;
  xmin = INT_MAX, ymin = INT_MAX, zmin = INT_MAX;

  int x, y, z;
  long idx;

  for (x = 0; x < xdim; x++)
  {
    for (y = 0; y < ydim; y++)
    {
      for (z = 0; z < zdim; z++)
      {

        // Dimensions of inr data are assumed to be in z,y,x order here
        // Not 100% sure this will be true in all cases?

        idx = get_array_index(z, y, x);
        // Is this element non 0 i.e. not background
        // if so check if the min/max values should be updated
        if (image_data[idx] != 0)
        {

          if (x > xmax)
            xmax = x;
          if (x < xmin)
            xmin = x;

          if (y > ymax)
            ymax = y;
          if (y < ymin)
            ymin = y;

          if (z > zmax)
            zmax = z;
          if (z < zmin)
            zmin = z;
        }
      }
    }
  }

  printf("Bounds of image are X: %d %d  Y: %d %d  Z: %d %d\n", xmin, xmax,
         ymin, ymax, zmin, zmax);

  xmid = (xmin + xmax) / 2;
  ymid = (ymin + ymax) / 2;
  zmid = (zmin + zmax) / 2;
}
