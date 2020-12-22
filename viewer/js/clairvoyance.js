// Axel '0vercl0k' Souchet - December 18 2020
class Clairvoyance_t {
    constructor(Canvas, ClickLog, MouseLog) {

        //
        // The log DOM element is where we can show to the user
        // the virtual-address and the coordinates.
        //

        this.ClickLog_ = ClickLog;
        this.MouseLog_ = MouseLog;

        //
        // The canvas is where we render the pixels.
        //

        this.Canvas_ = Canvas;
        this.Ctx_ = this.Canvas_.getContext('2d');

        //
        // Width, Height of the canvas.
        //

        this.Width_ = undefined;
        this.Height_ = undefined;

        //
        // Order of the Hilbert curve.
        //

        this.Order_ = undefined;

        //
        // This stores the last pixel clicked; it allows us
        // to restore its color when clicking somewhere else.
        //

        this.PixelClick_ = {
            X: undefined,
            Y: undefined,
            Color: undefined,
            HiColor: 0x082567
        };

        //
        // This stores the last pixel mouseover'd; it allows us
        // to restore its original color when moving the mouse
        // somewhere else.
        //

        this.PixelMouseOver_ = {
            X: undefined,
            Y: undefined,
            Color: undefined,
            HiColor: 0xFFFFFF,
        };

        //
        // Initialize the palette variables.
        //

        //
        // This maps value to color.
        //

        this.Palette_ = new Map();

        //
        // This maps color to name.
        //

        this.ReversePalette_ = new Map();

        const Names = {
            // Black
            'None': 0x00000000,
            // PaleGreen
            'UserRead': 0xa9ff52,
            // CanaryYellow
            'UserReadExec': 0xffff99,
            // Mauve
            'UserReadWrite': 0xe0b0ff,
            // LightRed
            'UserReadWriteExec': 0xff7f7f,
            // Green
            'KernelRead': 0x00ff00,
            // Yellow
            'KernelReadExec': 0xffff00,
            // Purple
            'KernelReadWrite': 0xa020f0,
            // Red
            'KernelReadWriteExec': 0xfe0000
        };

        let Idx = 0;
        for (const [Name, Color] of Object.entries(Names)) {
            this.Palette_.set(Idx, Color);
            this.ReversePalette_.set(Color, Name);
            Idx++;
        }

        this.Regions_ = [];
    }

    //
    // This is code that I stole from "Hacker's Delight" figure 16â€“8.
    // Calculate the distance from a set of (X, Y) coordinate.
    //

    xy2d(X_, Y_) {
        let[X,Y,S] = [X_, Y_, 0];
        for (let Idx = this.Order_ - 1; Idx >= 0; Idx--) {
            const Xi = (X >> Idx) & 1;
            const Yi = (Y >> Idx) & 1;
            if (Yi == 0) {
                const Tmp = X;
                X = Y ^ (-Xi);
                Y = Tmp ^ (-Xi);
            }
            S = 4 * S + 2 * Xi + (Xi ^ Yi);
        }

        return S;
    }

    //
    // This is code that I stole from "Hacker's Delight" figure 16â€“8.
    // Calculate (X, Y) coordinates from a distance.
    //

    d2xy(Dist) {
        let[S,Sr,Cs,Swap,Comp,T] = [0, 0, 0, 0, 0, 0];
        S = Dist | (0x55555555 << 2 * this.Order_);
        Sr = (S >> 1) & 0x55555555;
        Cs = ((S & 0x55555555) + Sr) ^ 0x55555555;
        Cs ^= (Cs >> 2);
        Cs ^= (Cs >> 4);
        Cs ^= (Cs >> 8);
        Cs ^= (Cs >> 16);
        Swap = Cs & 0x55555555;
        Comp = (Cs >> 1) & 0x55555555;
        T = (S & Swap) ^ Comp;
        S ^= Sr ^ T ^ (T << 1);
        S &= (1 << (2 * this.Order_)) - 1;
        T = (S ^ (S >> 1)) & 0x22222222;
        S ^= T ^ (T << 1);
        T = (S ^ (S >> 2)) & 0x0C0C0C0C;
        S ^= T ^ (T << 2);
        T = (S ^ (S >> 4)) & 0x00F000F0;
        S ^= T ^ (T << 4);
        T = (S ^ (S >> 8)) & 0x0000FF00;
        S ^= T ^ (T << 8);
        return {
            X: S >> 16,
            Y: S & 0xFFFF
        };
    }

    //
    // Parse a clairvoyance file.
    //

    parseFile(Content) {
        const Lines = Content.split('\n');
        if (Lines.length == 0) {
            throw `Failed to parse input file: no header`;
        }

        //
        // The header contains the width / height in pixels.
        //

        [this.Width_, this.Height_] = Lines[0].split(' ', 2).map(Number);
        if (isNaN(this.Width_) || isNaN(this.Height_)) {
            throw `Failed to parse input file: no header`;
        }

        //
        // Set the canvas' dimensions.
        //

        this.Canvas_.width = this.Width_;
        this.Canvas_.height = this.Height_;

        //
        // Calculate the order of the curve.
        //

        this.Order_ = Math.log2(this.Width_);
        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);
        let Distance = 0;

        //
        // Walk the lines; either it specifies the protection
        // of a page, or it specifies the address of the current
        // region.
        //

        for (const Line of Lines.slice(1)) {

            //
            // If it starts with '0x', this is the address of the current
            // region.
            //

            if (Line.startsWith('0x')) {

                //
                // If we have a new region we keep track of its start.
                //

                this.Regions_.push({
                    Va: BigInt(Line),
                    Start: BigInt(Distance),
                });

                continue;
            }

            //
            // The line is a page protection. In order to color the pixel,
            // we need to calculate which pixel that is on the canvas using
            // its distance.
            //

            const Coord = this.d2xy(Distance);

            //
            // Once we have the coordinates, we can calculate its offset as well
            // as its color.
            //

            const Protection = Number(Line);
            if (isNaN(Protection)) {
                throw `Failed to parse the file: the protection is not a number`;
            }

            const Color = this.Palette_.get(Protection);
            this.setPixelColor(ImgData, Coord.X, Coord.Y, Color);
            Distance++;

            //
            // If the clairvoyance file specifies more pages that fit onto the
            // curve, we bail.
            // XXX: Technically here, I think we should create a second canvas
            // to be able to represent the rest of the pixels. It could be shown
            // to the user as a 'second' page like in a book for example.
            //

            if (Distance == (this.Width_ * this.Height_)) {
                break;
            }
        }

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Define the events we'll handle.
        //

        this.Canvas_.onclick = async Event => {
            const Coord = this.getMousePos(Event);
            const Va = this.click(Coord.X, Coord.Y);
            await navigator.clipboard.writeText(`${Va.toString(16)}`);
        };

        this.Canvas_.onmousemove = Event => {
            const Coord = this.getMousePos(Event);
            this.mouseMove(Coord.X, Coord.Y);
        };

        return [this.Width_, this.Height_];
    }

    //
    // Get the mouse position from the event.
    //

    getMousePos(Event) {
        const Rect = this.Canvas_.getBoundingClientRect();
        return {
            X: (((Event.clientX - Rect.left) / (Rect.right - Rect.left)) * this.Canvas_.width) | 0,
            Y: (((Event.clientY - Rect.top) / (Rect.bottom - Rect.top)) * this.Canvas_.height) | 0
        };
    }

    //
    // Calculate the address from coordinates.
    //

    addressFromCoord(X, Y) {
        const Distance = BigInt(this.xy2d(X, Y));
        for (const [Idx,Region] of this.Regions_.entries()) {
            const NextRegion = this.Regions_[Idx + 1];
            if (NextRegion == undefined || Distance < NextRegion.Start) {
                const Va = Region.Va + ((Distance - Region.Start) * 0x1000n);
                return Va;
            }
        }

        return undefined;
    }

    //
    // Set a pixel to a specific color.
    //

    setPixelColor(ImgData, X, Y, Color) {

        //
        // Calculate the offset of the pixel.
        //

        const Offset = ((Y * this.Width_) + X) * 4;
        ImgData.data[Offset + 0] = (Color >> 16) & 0xff;
        ImgData.data[Offset + 1] = (Color >> 8) & 0xff;
        ImgData.data[Offset + 2] = (Color >> 0) & 0xff;
        ImgData.data[Offset + 3] = 0xff;
    }

    //
    // Save a pixel color.
    //

    savePixelColor(ImgData, X, Y) {

        //
        // Calculate the offset of the pixel.
        //

        const Offset = ((Y * this.Width_) + X) * 4;
        return (ImgData.data[Offset + 0] << 16) | (ImgData.data[Offset + 1] << 8) | (ImgData.data[Offset + 2] << 0);
    }

    //
    // Highlight a clicked pixel.
    //

    click(X, Y) {

        //
        // Calculate the virtual address at this point.
        //

        const Va = this.addressFromCoord(X, Y);
        if (Va == undefined) {
            throw `addressFromCoord failed`;
        }

        //
        // Get the canvas' content.
        //

        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);

        //
        // Restore the old pixel's color.
        //

        this.setPixelColor(ImgData, this.PixelClick_.X, this.PixelClick_.Y, this.PixelClick_.Color);

        //
        // Save the one we're about to highlight. It's trickier than one would think though.
        // The issue is that because we track the mouse moving, there's a good chance that
        // the current pixel is being highlighted; in which case we would read the highlight
        // color instead of its origin color.
        // The trick is to check if the current position equals the one of the last move over,
        // in which case we steal the color from there.
        //

        this.PixelClick_.X = X;
        this.PixelClick_.Y = Y;
        if (this.PixelMouseOver_.X == X && this.PixelMouseOver_.Y == Y) {
            this.PixelClick_.Color = this.PixelMouseOver_.Color;
        } else {
            this.PixelClick_.Color = this.savePixelColor(ImgData, X, Y);
        }

        //
        // Change the pixel color.
        //

        this.setPixelColor(ImgData, X, Y, this.PixelClick_.HiColor);

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Refresh the text log.
        //

        const Protection = this.ReversePalette_.get(this.PixelClick_.Color);
        this.ClickLog_.innerText = `Last clicked: ${Va.toString(16)} (${Protection})`;
        return Va;
    }

    //
    // Highlight a pixel when the mouse is moving.
    //

    mouseMove(X, Y) {

        //
        // Calculate the virtual address at this point.
        //

        const Va = this.addressFromCoord(X, Y);
        if (Va == undefined) {
            throw `addressFromCoord failed`;
        }

        //
        // Get the canvas' content.
        //

        const ImgData = this.Ctx_.getImageData(0, 0, this.Width_, this.Height_);

        //
        // Carry on with highlighting only if this pixel hasn't been clicked on,
        // otherwise it means we would remove its color.
        //

        if (this.PixelMouseOver_.X != this.PixelClick_.X || this.PixelMouseOver_.Y != this.PixelClick_.Y) {

            //
            // Restore the old pixel's color.
            //

            this.setPixelColor(ImgData, this.PixelMouseOver_.X, this.PixelMouseOver_.Y, this.PixelMouseOver_.Color);
        }

        if (X != this.PixelClick_.X || Y != this.PixelClick_.Y) {

            //
            // Save the one we're about to highlight.
            //

            this.PixelMouseOver_.X = X;
            this.PixelMouseOver_.Y = Y;
            this.PixelMouseOver_.Color = this.savePixelColor(ImgData, X, Y);

            //
            // Change the pixel color if we haven't clicked on it.
            //

            this.setPixelColor(ImgData, X, Y, this.PixelMouseOver_.HiColor);
        }

        //
        // Update the canvas' content.
        //

        this.Ctx_.putImageData(ImgData, 0, 0);

        //
        // Refresh the text log.
        //

        const Protection = this.ReversePalette_.get(this.PixelMouseOver_.Color);
        this.MouseLog_.innerText = `${Va.toString(16)} (${Protection})`;
    }
}
