#include "macutils.h"
#import <Cocoa/Cocoa.h>

void hideDockIcon() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}
