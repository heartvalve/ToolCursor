/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile: vtkPushPlaneAction.cxx,v $

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkPushPlaneAction.h"
#include "vtkObjectFactory.h"

#include "vtkSurfaceCursor.h"
#include "vtkVolumePicker.h"
#include "vtkProp3DCollection.h"
#include "vtkImageActor.h"
#include "vtkVolumeMapper.h"
#include "vtkPlaneCollection.h"
#include "vtkPlane.h"
#include "vtkTransform.h"
#include "vtkImageData.h"
#include "vtkCamera.h"
#include "vtkRenderer.h"
#include "vtkMath.h"

vtkCxxRevisionMacro(vtkPushPlaneAction, "$Revision: 1.3 $");
vtkStandardNewMacro(vtkPushPlaneAction);

//----------------------------------------------------------------------------
vtkPushPlaneAction::vtkPushPlaneAction()
{
  this->Transform = vtkTransform::New();

  this->ImageActor = 0;
  this->VolumeMapper = 0;
  this->Mapper = 0;
  this->PlaneId = -1;
  this->PerpendicularPlane = 0;

  this->StartNormal[0] = 0.0;
  this->StartNormal[1] = 0.0;
  this->StartNormal[2] = 1.0;

  this->StartOrigin[0] = 0.0;
  this->StartOrigin[1] = 0.0;
  this->StartOrigin[2] = 0.0;
}

//----------------------------------------------------------------------------
vtkPushPlaneAction::~vtkPushPlaneAction()
{
  this->Transform->Delete();
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::StartAction()
{
  this->Superclass::StartAction();

  // Get all the necessary information about the picked prop.
  this->GetPropInformation();

  // Check whether the normal is perpendicular to the view plane.
  // If it is, then we can't use the usual interaction calculations.
  this->PerpendicularPlane = 0;
  double normal[3];
  this->GetStartNormal(normal);

  vtkCamera *camera = this->SurfaceCursor->GetRenderer()->GetActiveCamera();
  double position[3], focus[3];
  camera->GetPosition(position);
  camera->GetFocalPoint(focus);

  double v[3];
  v[0] = focus[0] - position[0]; 
  v[1] = focus[1] - position[1]; 
  v[2] = focus[2] - position[2]; 

  vtkMath::Normalize(v);
  vtkMath::Normalize(normal);

  // This gives the sin() of the angle between the vectors.
  vtkMath::Cross(normal, v, v);
  if (vtkMath::Dot(v, v) < 0.2)
    {
    this->PerpendicularPlane = 1;
    }
} 

//----------------------------------------------------------------------------
void vtkPushPlaneAction::StopAction()
{
  this->Superclass::StopAction();
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::DoAction()
{
  this->Superclass::DoAction();

  if (!this->IsPlaneValid())
    {
    return;
    }

  // Get and normalize the plane normal.
  double normal[3];
  this->GetStartNormal(normal);
  vtkMath::Normalize(normal);

  // Get the depth coordinate from the original pick.
  double ox, oy, oz;
  this->WorldToDisplay(this->StartPosition, ox, oy, oz);

  // Get the initial display position.
  ox = this->StartDisplayPosition[0];
  oy = this->StartDisplayPosition[1];

  // Get the current display position. 
  double x = this->DisplayPosition[0];
  double y = this->DisplayPosition[1];

  // If plane is perpendicular, we only use the "x" motion.
  if (this->PerpendicularPlane) { y = oy; };

  // Get world point for the start position.
  double p1[3];
  this->DisplayToWorld(ox, oy, oz, p1);

  // Get the view ray for the current position, using old depth.
  double p2[3], viewRay[3];
  this->GetViewRay(x, y, oz, p2, viewRay);

  // The push distance, which is what we aim to calculate.
  double distance = 0.0;

  // Special action if the plane is perpendicular to view normal.
  if (this->PerpendicularPlane)
    {
    // Calculate distance moved in world coordinates.
    distance = sqrt(vtkMath::Distance2BetweenPoints(p1, p2));
    if (x - ox < 0)
      {
      distance = -distance;
      }

    // Check whether the normal is towards or away from the camera.
    if (vtkMath::Dot(viewRay, normal) < 0)
      {
      distance = -distance;
      }
    }
  else
    {
    // Get the vector between origin world point and current world point.
    double u[3];
    u[0] = p2[0] - p1[0];
    u[1] = p2[1] - p1[1];
    u[2] = p2[2] - p1[2];

    // Finally, I thought up a perfectly intuitive "push" action method.
    // The trick here is as follows: we need to find the position along
    // the plane normal (starting at the original pick position) that is
    // closest to the view-ray line at the current mouse position.  Not
    // sure if VTK has a math routine for this, so do calculations here:
    double a = vtkMath::Dot(normal, normal);
    double b = vtkMath::Dot(normal, viewRay);
    double c = vtkMath::Dot(viewRay, viewRay);
    double d = vtkMath::Dot(normal, u);
    double e = vtkMath::Dot(viewRay, u);

    distance = (c*d - b*e)/(a*c - b*b);
    }

  // Moving relative to the original position provides a more stable 
  // interaction that moving relative to the last position.
  double origin[3];
  this->GetStartOrigin(origin);
  origin[0] = origin[0] + distance*normal[0];
  origin[1] = origin[1] + distance*normal[1];
  origin[2] = origin[2] + distance*normal[2];

  this->SetOrigin(origin);
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::GetPropInformation()
{
  // Get all the object needed for the interaction
  vtkVolumePicker *picker = this->SurfaceCursor->GetPicker();

  vtkProp3DCollection *props = picker->GetProp3Ds();
  vtkCollectionSimpleIterator pit;
  props->InitTraversal(pit);
  vtkProp3D *prop = props->GetNextProp3D(pit);

  this->Transform->SetMatrix(prop->GetMatrix());
  this->ImageActor = vtkImageActor::SafeDownCast(prop);
  this->Mapper = picker->GetMapper();
  this->VolumeMapper = vtkVolumeMapper::SafeDownCast(this->Mapper);

  // Initialize plane to "no plane" value
  this->PlaneId = -1;

  // Is this a VolumeMapper cropping plane or AbstractMapper clipping plane?
  if (this->VolumeMapper && picker->GetCroppingPlaneId() >= 0)
    {
    this->Mapper = 0;
    this->PlaneId = picker->GetCroppingPlaneId();
    }
  else
    {
    this->VolumeMapper = 0;
    this->PlaneId = picker->GetClippingPlaneId();
    }

  // Create a PlaneId for image actor.
  if (this->ImageActor)
   {
   int extent[6];
   this->ImageActor->GetDisplayExtent(extent);
   this->PlaneId = 4;
   if (extent[2] == extent[3]) { this->PlaneId = 2; }
   else if (extent[0] == extent[1]) { this->PlaneId = 0; }
   }

  if (this->PlaneId >= 0)
    {
    this->GetPlaneOriginAndNormal(this->StartOrigin, this->StartNormal);
    }
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::GetPlaneOriginAndNormal(double origin[3],
                                                 double normal[3])
{
  if (this->Mapper)
    {
    vtkPlane *plane =
      this->Mapper->GetClippingPlanes()->GetItem(this->PlaneId);

    plane->GetNormal(normal);
    plane->GetOrigin(origin);
    }
  else
    {
    double bounds[6];

    if (this->ImageActor)
      {
      this->ImageActor->GetDisplayBounds(bounds);
      }
    else if (this->VolumeMapper)
      {
      this->VolumeMapper->GetCroppingRegionPlanes(bounds);
      }
    else
      {
      return;
      }

    int i = this->PlaneId/2;

    normal[0] = normal[1] = normal[2] = 0.0;
    normal[i] = 1.0;

    origin[0] = 0.5*(bounds[0] + bounds[1]);
    origin[1] = 0.5*(bounds[2] + bounds[3]);
    origin[2] = 0.5*(bounds[4] + bounds[5]);
    origin[i] = bounds[this->PlaneId];

    // Transform from data coords to world coords.
    this->Transform->TransformNormal(normal, normal);
    this->Transform->TransformPoint(origin, origin);
    }
}

//----------------------------------------------------------------------------
void vtkPushPlaneAction::SetOrigin(const double o[3])
{
  if (this->PlaneId < 0)
    {
    return;
    }

  // Respect constness: make a copy that we can modify.
  double origin[3];
  origin[0] = o[0];
  origin[1] = o[1];
  origin[2] = o[2];

  if (this->Mapper)
    {
    vtkPlane *plane =
      this->Mapper->GetClippingPlanes()->GetItem(this->PlaneId);

    // Bounding checks needed!

    plane->SetOrigin(origin);
    }
  else
    {
    // Go from world coords to data coords
    this->Transform->GetInverse()->TransformPoint(origin, origin);

    int i = this->PlaneId/2;

    if (this->ImageActor)
      {
      double dataOrigin[3];
      this->ImageActor->GetInput()->GetOrigin(dataOrigin);
      double dataSpacing[3];
      this->ImageActor->GetInput()->GetSpacing(dataSpacing);
      int wholeExtent[6];
      this->ImageActor->GetInput()->GetWholeExtent(wholeExtent);
      int displayExtent[6];
      this->ImageActor->GetDisplayExtent(displayExtent);
      
      double x = (origin[i] - dataOrigin[i])/dataSpacing[i];
      if (x < wholeExtent[2*i]) { x = wholeExtent[2*i]; }
      if (x > wholeExtent[2*i+1]) { x = wholeExtent[2*i+1]; }
 
      int xi = int(floor(x));
      if ((x - xi) >= 0.5) { xi++; }

      displayExtent[2*i] = displayExtent[2*i+1] = xi; 
      this->ImageActor->SetDisplayExtent(displayExtent);
      }
    else if (this->VolumeMapper)
      {
      double region[6];
      this->VolumeMapper->GetCroppingRegionPlanes(region);
      double bounds[6];
      this->VolumeMapper->GetBounds(bounds);

      // Get the cropping plane position
      double x = origin[i];

      // Get the minimum thickness of volume allowed.
      double t = 1.0;
      vtkImageData *data = this->VolumeMapper->GetInput();
      if (data)
        {
        double spacing[3];
        data->GetSpacing(spacing);
        t = spacing[i];
        }

      // Check for collissions with the opposing plane
      if (this->PlaneId == 2*i)
        {
        if (x > region[2*i+1] - t) { x = region[2*i+1] - t; }
        if (x > bounds[2*i+1] - t) { x = bounds[2*i+1] - t; }
        }
      else
        {
        if (x < region[2*i] + t) { x = region[2*i] + t; }
        if (x < bounds[2*i] + t) { x = bounds[2*i] + t; }
        }

      // Bounding box check
      if (x < bounds[2*i]) { x = bounds[2*i]; }
      if (x > bounds[2*i+1]) { x = bounds[2*i+1]; }

      region[this->PlaneId] = x;
      this->VolumeMapper->SetCroppingRegionPlanes(region);
      }
    }
}   

//----------------------------------------------------------------------------
void vtkPushPlaneAction::SetNormal(const double n[3])
{
  if (this->PlaneId < 0)
    {
    return;
    }

  // Respect constness: make a copy rather than using ugly const cast.
  double normal[3];
  normal[0] = n[0];
  normal[1] = n[1];
  normal[2] = n[2];

  // Setting the normal is only valid for the mapper clipping planes.
  if (this->Mapper)
    {
    vtkPlane *plane =
      this->Mapper->GetClippingPlanes()->GetItem(this->PlaneId);

    // Sanity checks needed!

    plane->SetNormal(normal);
    }
}
