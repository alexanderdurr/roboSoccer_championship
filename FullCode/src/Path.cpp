#include "lib/Path.h"
#include "lib/Physics.h"


Path::Path(Physics* physics, int id)
    :
      physics_(physics),
      id_(id),
      tree_(NULL)
{
}

Path::~Path()
{
}


void Path::initializePath() // Called in initializePhysics()
{
    // Game Field
    field_ = physics_->getObstacleFieldPtr();
    // Corners
    corners_.push_back(physics_->getObstacleCornerBottomLeftPtr());
    corners_.push_back(physics_->getObstacleCornerBottomRightPtr());
    corners_.push_back(physics_->getObstacleCornerTopLeftPtr());
    corners_.push_back(physics_->getObstacleCornerTopRightPtr());
}


void Path::compute(TargetPoint requestedEnd)
{
    // Init, compute directly overwrites CAtargetPoints_
    CAtargetPoints_.clear();
    Position start = position_.toPosition();
    bool startingInObstacle = false;
    Position end = requestedEnd.Location.toPosition();
    //cout << "[CA] Requested end given to CA: " << end << endl;

    // Check for invalid start/end positions
    for (auto i : obstacles_)
    {
        //cout << "Obstacle: " << *i << endl;
        if (i->isInside(end))
        {
            end = i->getValidPosition(end);
            break;
        }
    }

    if (useGameField_) {
        if (!field_->isInside(end))
        {
            LineSegment toCenter(end, field_->getCenter());
            Position overwrite = field_->getIntersection(toCenter).front().toPosition();
            //cout << "[CA] Target not inside field! Old: " << end << " New: " << overwrite;
            end = overwrite;
        }
    }

    for (auto i : corners_)
    {
        if (i->isInside(end))
        {
            LineSegment toCenter(end, field_->getCenter());
            Position overwrite = i->getIntersection(toCenter).front().toPosition();
            //cout << "[CA] Target inside corner! Old: " << end << " New: " << overwrite;
            end = overwrite;
            break;
        }
    }


    // Check if direct path is free
    LineSegment goalSeg(start, end);
    if (!intersectsObstacle(goalSeg))
    {
        CAtargetPoints_.push_back(TargetPoint(end));
        //cout << "[CA] Direct path found: " << CAtargetPoints_.front().Location << endl;
        return;
    }


    // Initialize k-d tree
    delete tree_;
    tree_ = new KdTree(start, startingInObstacle);
    bool pathCompleted = false;
    const Node *nearestNode;
    double resetStepSize = stepSize_;
    //cout << "[CA] KdTree initialized! Root: " << tree_->root()->position() << endl;

    for (int iteration = 1; iteration < nr_iterations_ && !pathCompleted; iteration++)
    {
        if (iteration == nr_iterations_ - 1)
        {
               // cout << "[CA] Iteration limit reached!" << endl;
        }

        // Select target point
        Vector2d target;
        const double p = ((double) std::rand() / (RAND_MAX));
        if (p < p_dest_) {
            target = end;
        }
        else
        {
            target = randomState();
        }

        // Find nearest Node to target point
        nearestNode = tree_->nearest(target);

        // Extend tree to target
        const Node *extendedNode = extend(nearestNode, target);
        if (extendedNode == NULL) {
            stepSize_ += stepSize_*0.05;
            //cout << "[CA] Extend returned NULL -> stepSize " << stepSize_ << endl;
            continue;
        }
        stepSize_ = resetStepSize;


        // Check if target was reached
        double dist = extendedNode->position().getDistance(end);
        if (dist < 0.001)
        {
            pathCompleted = true;
            nearestNode = extendedNode;
            //cout << "[CA] Path completed, nearest Node: " << nearestNode->position() << " distance: " << dist << " end: " << end << endl;
            break;
        }

    }
    stepSize_ = resetStepSize;

    nearestNode = tree_->nearest(end);

    // Add waypoints to targetPoints_
    while (nearestNode)
    {
        //Not push_back as nearest node would be last to drive to
        CAtargetPoints_.insert(CAtargetPoints_.begin(), TargetPoint(nearestNode->position()));
        nearestNode = tree_->previous(nearestNode);
    }

    // Post-processing
    for (int i = 0; i < nr_pp_steps_; ++i)
    {
        simplify();
        cutCorners();
    }
    // Final cleanup
    simplify();
}


void Path::simplify()
{
    for (u_int start_index = 0; start_index < CAtargetPoints_.size(); start_index++)
    {
        for (u_int end_index = CAtargetPoints_.size() - 1; end_index > start_index + 1; end_index--)
        {
            LineSegment seg(CAtargetPoints_[start_index].Location, CAtargetPoints_[end_index].Location);
            if (!intersectsObstacle(seg))
            {
                for (u_int i = 0; i < end_index - start_index - 1; i++) {
                    CAtargetPoints_.erase(CAtargetPoints_.begin() + start_index + 1);
                }
                break;
            }
        }
    }

}


void Path::cutCorners()
{
    for (u_int i = 1; i < CAtargetPoints_.size() - 1; i++)
    {
        Vector2d left = CAtargetPoints_[i - 1].Location;
        Vector2d mid = CAtargetPoints_[i].Location;
        Vector2d right = CAtargetPoints_[i + 1].Location;
        Vector2d diffLeft = left - mid;
        Vector2d diffRight = right - mid;
        // Max corner cutting distance
        double step = std::min(diffLeft.getLength(), diffRight.getLength());
        diffLeft = diffLeft.getNormalized();
        diffRight = diffRight.getNormalized();

        // Not neccessarily optimal
        step /= 2;
        double dist = step;
        double lastGood = 0.;

        while (step > 0.01)
        {
            // Symmetrical corner cutting
            LineSegment line(mid + diffLeft * dist, mid + diffRight * dist);
            step /= 2;
            if (!intersectsObstacle(line))
            {
                lastGood = dist;
                dist += step;
            }
            else
            {
                dist -= step;
            }
        }

        if (lastGood > 0.0)
        {
            // Cut cut corner
            CAtargetPoints_[i].Location = mid + diffLeft * lastGood;
            CAtargetPoints_.insert(CAtargetPoints_.begin() + i, TargetPoint(mid + diffRight * lastGood));
            i++;
        }
    }
}


Vector2d Path::randomState() const
{
    Vector2d random(((double) std::rand() / (RAND_MAX)), ((double) std::rand() / (RAND_MAX)));
    random.x = random.x * (1.385 + 1.425) - 1.425;
    random.y = random.y * (0.880 + 0.882) - 0.880;
    return random;
}



const Node* Path::extend(const Node *fromNode, const Vector2d &to)
{
    const Vector2d &from = tree_->position(fromNode);
    //const bool inObstacle = tree_->inObstacle(fromNode);
    Vector2d d = to - from;
    const double l = d.getLength();
    if (l == 0)
    {
        //cout << "[CA] Extend: point already reached" << endl;
        return NULL;
    }
    else if (l > stepSize_)
    {
        // not reachable in one step
        d = d * stepSize_ / l;
    }
    const Vector2d extended = from + d;

    // Test if new path is valid
    if (useGameField_)
    {
        if (!field_->isInside(extended.toPosition()))
        {
            //cout << "[CA] Extend: point is not inside field" << endl;
            return NULL;
        }
    }
    for (auto i : corners_)
    {
        if (i->isInside(extended.toPosition()))
        {
            return NULL;
        }
    }
    for (auto i : obstacles_)
    {
        if (i->isInside(extended.toPosition()))
        {
            //cout << "[CA] Extend: point inside obstacle" << endl;
            return NULL;
        }
    }

    return tree_->insert(extended, false, fromNode);
}


bool Path::intersectsObstacle(const LineSegment& seg) const
{
    for (auto i : obstacles_)
    {
        if (i->intersects(seg))
        {
            return true;
        }
    }
    return false;
}


bool Path::intersectsObstacle(const LineSegment& seg, int idx) const
{
    int k = 0;
    for (auto i : obstacles_)
    {
        if (dynamic_cast<Quadrangle*>(i))
        {
            //std::cout << "Cast true " << *i << std::endl;
            if (i->intersects(seg))
            {
                return true;
            }
        }
        else
        {
            if (k != idx && i->intersects(seg))
            {
                return true;
            }
        }

        k++;
    }
    return false;
}

