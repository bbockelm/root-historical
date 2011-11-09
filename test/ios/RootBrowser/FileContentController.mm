#import <QuartzCore/QuartzCore.h>

#import "FileContentController.h"
#import "ROOTObjectController.h"
#import "SlideshowController.h"
#import "ObjectShortcut.h"
#import "Shortcuts.h"


//C++ imports.
#import "IOSPad.h"

#import "FileUtils.h"

@implementation FileContentController {
   NSMutableArray *objectShortcuts;
}

@synthesize fileContainer;

//____________________________________________________________________________________________________
- (id)initWithNibName : (NSString *)nibNameOrNil bundle : (NSBundle *)nibBundleOrNil
{
   self = [super initWithNibName : nibNameOrNil bundle : nibBundleOrNil];

   if (self) {
      [self view];
      objectShortcuts = [[NSMutableArray alloc] init];
      self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Slide show" style:UIBarButtonItemStyleBordered target:self action:@selector(startSlideshow)];
   }

   return self;
}

//____________________________________________________________________________________________________
- (void)didReceiveMemoryWarning
{
   // Releases the view if it doesn't have a superview.
   [super didReceiveMemoryWarning];
    
   // Release any cached data, images, etc that aren't in use.
}

#pragma mark - View lifecycle


//____________________________________________________________________________________________________
- (void) correctFramesForOrientation : (UIInterfaceOrientation) orientation
{
   CGRect mainFrame;
   CGRect scrollFrame;

   if (orientation == UIInterfaceOrientationPortrait || orientation == UIInterfaceOrientationPortraitUpsideDown) {
      mainFrame = CGRectMake(0.f, 0.f, 768.f, 1004.f);
      scrollFrame = CGRectMake(0.f, 44.f, 768.f, 960.f);
   } else {
      mainFrame = CGRectMake(0.f, 0.f, 1024.f, 748.f);
      scrollFrame = CGRectMake(0.f, 44.f, 1024.f, 704.f);   
   }
   
   self.view.frame = mainFrame;
   scrollView.frame = scrollFrame;
   
   if ([[scrollView subviews] count])
      [ShorcutUtil placeShortcuts : objectShortcuts inScrollView : scrollView withSize : CGSizeMake([ObjectShortcut iconWidth], [ObjectShortcut iconHeight] + [ObjectShortcut textHeight]) andSpace : 100.f];
}

//____________________________________________________________________________________________________
- (void) viewWillAppear:(BOOL)animated
{
   //self.interfaceOrientation ?
   [self correctFramesForOrientation : [UIApplication sharedApplication].statusBarOrientation];
}

//____________________________________________________________________________________________________
- (void)viewDidLoad
{
   [super viewDidLoad];
//   [self correctFrames];
   // Do any additional setup after loading the view from its nib.
}

//____________________________________________________________________________________________________
- (void)viewDidUnload
{
   [super viewDidUnload];
   // Release any retained subviews of the main view.
   // e.g. self.myOutlet = nil;
}

//____________________________________________________________________________________________________
- (BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)interfaceOrientation
{
   // Return YES for supported orientations
	return YES;
}

//____________________________________________________________________________________________________
- (void)willAnimateRotationToInterfaceOrientation : (UIInterfaceOrientation)interfaceOrientation duration:(NSTimeInterval)duration {
   [self correctFramesForOrientation : interfaceOrientation];
}

//____________________________________________________________________________________________________
- (void) clearScrollview
{
   NSArray *viewsToRemove = [scrollView subviews];
   for (UIView *v in viewsToRemove)
      [v removeFromSuperview];

}

//____________________________________________________________________________________________________
- (void) addShortcutForObjectAtIndex : (unsigned) objIndex
{
   const CGRect rect = CGRectMake(0.f, 0.f, [ObjectShortcut iconWidth], [ObjectShortcut iconHeight]);
   UIGraphicsBeginImageContext(rect.size);
   CGContextRef ctx = UIGraphicsGetCurrentContext();
   if (!ctx) {
      UIGraphicsEndImageContext();
      return;
   }
      
   //Now draw into this context.
   CGContextTranslateCTM(ctx, 0.f, rect.size.height);
   CGContextScaleCTM(ctx, 1.f, -1.f);
      
   //Fill bitmap with white first.
   CGContextSetRGBFillColor(ctx, 0.f, 0.f, 0.f, 1.f);
   CGContextFillRect(ctx, rect);
   //Set context and paint pad's contents
   //with special colors (color == object's identity)
   ROOT::iOS::Pad *pad = fileContainer->GetPadAttached(objIndex);
   pad->cd();
   pad->SetViewWH(rect.size.width, rect.size.height);
   pad->SetContext(ctx);
   pad->PaintThumbnail();
   
   UIImage *thumbnailImage = UIGraphicsGetImageFromCurrentImageContext();//autoreleased UIImage.
   UIGraphicsEndImageContext();
       
   ObjectShortcut *shortcut = [[ObjectShortcut alloc] initWithFrame : [ObjectShortcut defaultRect] controller : self forObjectAtIndex:objIndex withThumbnail : thumbnailImage];
   shortcut.layer.shadowColor = [UIColor blackColor].CGColor;
   shortcut.layer.shadowOffset = CGSizeMake(20.f, 20.f);
   shortcut.layer.shadowOpacity = 0.3f;

   [scrollView addSubview : shortcut];
   [objectShortcuts addObject : shortcut];

   UIBezierPath *path = [UIBezierPath bezierPathWithRect : rect];
   shortcut.layer.shadowPath = path.CGPath;
}

//____________________________________________________________________________________________________
- (void) addObjectsIntoScrollview
{
   typedef ROOT::iOS::Browser::FileContainer::size_type size_type;

   [self clearScrollview];

   objectShortcuts = [[NSMutableArray alloc] init];

   for (size_type i = 0; i < fileContainer->GetNumberOfObjects(); ++i)
      [self addShortcutForObjectAtIndex : i];
}

//____________________________________________________________________________________________________
- (void) activateForFile : (ROOT::iOS::Browser::FileContainer *)container
{
   fileContainer = container;
   self.navigationItem.title = [NSString stringWithFormat : @"Contents of %s", container->GetFileName()];
   self.navigationItem.rightBarButtonItem.enabled = fileContainer->GetNumberOfObjects() > 1 ? YES : NO;
   
   //Prepare objects' thymbnails.
   [self addObjectsIntoScrollview];
   [self correctFramesForOrientation : [UIApplication sharedApplication].statusBarOrientation];
}

//____________________________________________________________________________________________________
- (void) startSlideshow
{
   SlideshowController *slideshowController = [[SlideshowController alloc] initWithNibName : @"SlideshowController" bundle : nil fileContainer : fileContainer];
   [self.navigationController pushViewController : slideshowController animated : YES];
}

//____________________________________________________________________________________________________
- (void) doTest
{
   const unsigned testIndex = 1 + rand() % (fileContainer->GetNumberOfObjects() - 1);
   ROOTObjectController *objectController = [[ROOTObjectController alloc] initWithNibName:@"ROOTObjectController" bundle : nil];
   [objectController setNavigationForObjectWithIndex : testIndex fromContainer : fileContainer];
   [self.navigationController pushViewController : objectController animated : YES];
}

//____________________________________________________________________________________________________
- (void) selectObjectFromFile : (ObjectShortcut *) shortcut
{
   ROOTObjectController *objectController = [[ROOTObjectController alloc] initWithNibName:@"ROOTObjectController" bundle : nil];
   [objectController setNavigationForObjectWithIndex : shortcut.objectIndex fromContainer : fileContainer];
   [self.navigationController pushViewController : objectController animated : YES];
}

@end
